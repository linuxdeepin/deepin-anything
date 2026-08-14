// SPDX-FileCopyrightText: 2024-2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/base_event_handler.h"

#include "core/disk_scanner.h"
#include "utils/log.h"
#include "utils/string_helper.h"
#include "utils/tools.h"
#include "utils/running_flag.h"

#include <QCoreApplication>
#include <glib/gstdio.h>

#define REFRESH_INDEX_FILE "refresh_index"
#define JOB_BATCH_COUNT_FOR_LIGHT_LOAD 4


static std::string get_refresh_index_path(const std::string &volatile_index_dir)
{
    return volatile_index_dir + "/" + REFRESH_INDEX_FILE;
}

static bool set_refresh_index_flag(const std::string &volatile_index_dir)
{
    std::string refresh_flag_path = get_refresh_index_path(volatile_index_dir);

    if (g_file_set_contents(refresh_flag_path.c_str(), "", -1, nullptr)) {
        spdlog::info("Refresh index flag set successfully at {}", refresh_flag_path);
        return true;
    } else {
        spdlog::warn("Failed to set refresh index flag at {}: {}", refresh_flag_path,
                     strerror(errno));
        return false;
    }
}

static bool have_refresh_index_flag(const std::string &volatile_index_dir)
{
    return g_file_test(get_refresh_index_path(volatile_index_dir).c_str(), G_FILE_TEST_EXISTS);
}

static bool remove_refresh_index_flag(const std::string &volatile_index_dir)
{
    std::string refresh_flag_path = get_refresh_index_path(volatile_index_dir);

    if (g_file_test(refresh_flag_path.c_str(), G_FILE_TEST_EXISTS)) {
        if (g_unlink(refresh_flag_path.c_str()) != 0) {
            spdlog::warn("Failed to remove refresh index flag at {}: {}", refresh_flag_path,
                         strerror(errno));
            return false;
        } else {
            spdlog::info("Refresh index flag removed successfully from {}", refresh_flag_path);
            return true;
        }
    } else {
        spdlog::debug("Refresh index flag file does not exist at {}, skipping removal",
                      refresh_flag_path);
        return true;
    }
}

base_event_handler::base_event_handler(const event_handler_config &config)
    : config_(config),
      index_manager_(config_.persistent_index_dir, config_.volatile_index_dir, config_.file_type_mapping),
      batch_size_(200),
      pool_(1),
      cancellable_(g_cancellable_new()),
      index_dirty_(false),
      commit_volatile_index_timeout_(config_.commit_volatile_index_timeout),
      index_status_(anything::index_status::loading),
      stop_scan_directory_(false),
      batch_count_(0),
      wait_commit_persistent_index_(false) {
    // 若发现索引数量为空或异常退出, 则设置 refresh_index 标志以触发扫盘逻辑
    // 索引版本号会保存为一条记录
    if (index_manager_.document_size(false) <= 1 || !is_last_time_normal_quit()) {
        spdlog::info("Set refresh index flag for empty index or abnormal quit");
        set_refresh_index_flag(config_.volatile_index_dir);
    }
}

base_event_handler::~base_event_handler() {
    g_object_unref(cancellable_);
}

void base_event_handler::terminate_processing() {
    g_cancellable_cancel(cancellable_);
    stop_scan_directory_ = true;

    if (timer_.joinable()) {
        auto thread_id = timer_.get_id();
        timer_.join();
        std::ostringstream oss;
        oss << thread_id;
        spdlog::info("Timer thread {} has exited", oss.str());
    }

    pool_.wait_for_tasks();
    if (!jobs_.empty()) {
        // Eat all jobs
        for (auto &&job : jobs_) {
            eat_job(std::move(job));
        }
        jobs_.clear();
    }
}

void base_event_handler::set_index_invalid_and_restart() {
    spdlog::info("Set index invalid and restart");

    index_manager_.set_index_invalid();

    set_app_restart(true);
    qApp->quit();
}

bool base_event_handler::handle_config_change(const std::string &key, const event_handler_config &new_config)
{
    if (key == "blacklist_paths") {
        set_blacklist_paths(new_config.blacklist_paths);
        return true;
    } else if (key == "pending_events_trigger_updating") {
        // 简单数据类型更新不加锁
        spdlog::info("pending_events_trigger_updating updated: {} -> {}",
                     config_.pending_events_trigger_updating,
                     new_config.pending_events_trigger_updating);
        config_.pending_events_trigger_updating = new_config.pending_events_trigger_updating;
        return true;
    } else {
        spdlog::info("Dynamic updates of config are not supported: {}", key);
        return false;
    }
}

gboolean base_event_handler::trigger_commit_persistent_index(base_event_handler *handler)
{
    // 不使用 jobs_push 避免将 index_dirty_ 置为 true
    std::lock_guard<std::mutex> lock(handler->jobs_mtx_);
    spdlog::debug("Push job: commit_persistent_index");
    handler->jobs_.emplace_back("", anything::index_job_type::commit_persistent_index);
    return FALSE;
}

void base_event_handler::set_batch_size(std::size_t size) {
    batch_size_ = size;
}

void base_event_handler::add_index_delay(std::string path) {
    jobs_push(std::move(path), anything::index_job_type::add);
}

void base_event_handler::remove_index_delay(std::string path) {
    jobs_push(std::move(path), anything::index_job_type::remove);
}

void base_event_handler::update_index_delay(std::string src, std::string dst) {
    jobs_push(std::move(src), anything::index_job_type::update, std::move(dst));
}

void base_event_handler::scan_index_delay(std::string path) {
    jobs_push(std::move(path), anything::index_job_type::scan);
}

void base_event_handler::recursive_update_index_delay(std::string src, std::string dst) {
    jobs_push(std::move(src), anything::index_job_type::recursive_update, std::move(dst));
}

void base_event_handler::init_scan_index_delay(std::string path) {
    index_status_ = anything::index_status::scanning;
    jobs_push(std::move(path), anything::index_job_type::init_scan);
}

void base_event_handler::refresh_indexes() {
    jobs_push("", anything::index_job_type::refresh);
}

// 启动时按需扫盘
// 若发现 refresh_index 文件存在, 则扫盘, 扫盘完成后会删除 refresh_index 文件
// 若发现 refresh_index 文件不存在, 则不扫盘
void base_event_handler::init_refresh_scan_indexes(std::vector<std::string>& index_dirs)
{
    if (have_refresh_index_flag(config_.volatile_index_dir)) {
        spdlog::info("Found refresh index flag, do index refresh and scan");

        // init refresh indexes
        index_dirty_ = index_manager_.refresh_indexes(get_blacklist_paths(), false, true);

        // add init scan event
        for (auto& dir : index_dirs) {
            add_index_delay(dir);
            init_scan_index_delay(std::move(dir));
        }
        // indicate init scan end
        init_scan_index_delay("");
    } else {
        spdlog::info("No found refresh index flag, skip index refresh and scan");

        for (const auto& dir : index_dirs) {
            start_handle_init_scan(dir);
        }

        index_status_ = anything::index_status::monitoring;

        // trigger timer thead save index status
        index_dirty_ = true;
    }

    timer_ = std::thread(&base_event_handler::timer_worker, this, 1000);
}

void base_event_handler::eat_jobs(std::vector<anything::index_job>& jobs, std::size_t number) {
    std::vector<anything::index_job> processing_jobs;

    processing_jobs.insert(
        processing_jobs.end(),
        std::make_move_iterator(jobs.begin()),
        std::make_move_iterator(jobs.begin() + number));
    jobs.erase(jobs.begin(), jobs.begin() + number);

    g_atomic_int_inc(&batch_count_);
    pool_.enqueue_detach([this, processing_jobs = std::move(processing_jobs)]() {
        for (const auto& job : processing_jobs) {
            eat_job(job);
        }
        g_atomic_int_dec_and_test(&this->batch_count_);
        check_jobs_load();
    });

}

void base_event_handler::eat_job(const anything::index_job& job) {
    bool ret = false;

    switch (job.type) {
        case anything::index_job_type::add:
            ret = index_manager_.add_index(job.src);
            break;
        case anything::index_job_type::remove:
            ret = index_manager_.remove_index(job.src);
            break;
        case anything::index_job_type::update:
            if (job.dst) {
                ret = index_manager_.update_index(job.src, *job.dst);
            }
            break;
        case anything::index_job_type::scan:
            ret = scan_directory(job.src, [this](const std::string& path) {
                return index_manager_.add_index(path);
            });
            break;
        case anything::index_job_type::recursive_update:
            if (job.dst) {
                auto src_subitems = index_manager_.traverse_directory(job.src, true, ret);
                if (!ret)
                    break;

                src_subitems.emplace_back(job.src);

                if (job.dst->empty()) {
                    for (auto const& src : src_subitems) {
                        ret = index_manager_.remove_index(src);
                        if (!ret)
                            break;
                    }
                } else {
                    size_t event_src_len = job.src.length();
                    for (auto const& src : src_subitems) {
                        std::string dst = src;
                        dst.replace(0, event_src_len, *job.dst);
                        ret = index_manager_.update_index(src, dst);
                        if (!ret)
                            break;
                    }
                }
            }
            break;
        case anything::index_job_type::init_scan:
            if (!job.src.empty()) {
                start_handle_init_scan(job.src);
                ret = scan_directory(job.src, [this](const std::string& path) {
                    if (!index_manager_.document_exists(path, true))
                        return index_manager_.add_index(path);
                    else
                        return true;
                });
            } else {
                // init scan end
                index_status_ = anything::index_status::monitoring;
                if (stop_scan_directory_) {
                    spdlog::info("Not remove refresh index flag for interrupted scan");
                } else {
                    remove_refresh_index_flag(config_.volatile_index_dir);
                }
                spdlog::info("Index scan completed");
                // index commit will be triggered by timer
                ret = true;
            }
            break;
        case anything::index_job_type::refresh:
            index_manager_.refresh_indexes(get_blacklist_paths(), true, false);
            ret = true;
            break;
        case anything::index_job_type::commit_volatile_index:
            spdlog::debug("Eat job: commit_volatile_index");
            if (g_atomic_int_get(&batch_count_) <= JOB_BATCH_COUNT_FOR_LIGHT_LOAD) {
                // 现在线程池中待处理的任务较少
                if (index_status_ == anything::index_status::updating)
                    index_status_ = anything::index_status::monitoring;
                ret = index_manager_.commit(index_status_);
                if (ret) {
                    if (!wait_commit_persistent_index_) {
                        wait_commit_persistent_index_ = true;
                        g_timeout_add_seconds (config_.commit_persistent_index_timeout,
                                               (GSourceFunc)trigger_commit_persistent_index,
                                               this);
                    }
                } else {
                    spdlog::error("Failed to commit index");
                }
            } else {
                ret = true;
                spdlog::debug("Skip to commit volatile index due to a large number of pending events");
            }
            break;
        case anything::index_job_type::commit_persistent_index:
            spdlog::debug("Eat job: commit_persistent_index");
            index_manager_.persist_index();
            wait_commit_persistent_index_ = false;
            ret = true;
            break;
        default:
            spdlog::error("Invalid job type: {}", static_cast<int>(job.type));
            break;
    }

    if (!ret) {
        spdlog::info("Failed to process job");
        set_index_invalid_and_restart();
    }
}

void base_event_handler::check_jobs_load()
{
    if ((g_atomic_int_get(&batch_count_)*(int)batch_size_) >= config_.pending_events_trigger_updating &&
        index_status_ == anything::index_status::monitoring) {
        index_status_ = anything::index_status::updating;
        index_manager_.set_index_updating();
        spdlog::info("Set index status to updating");
    }
}

void base_event_handler::jobs_push(std::string src,
    anything::index_job_type type, std::optional<std::string> dst) {

    std::lock_guard<std::mutex> lock(jobs_mtx_);
    index_dirty_ = true;
    jobs_.emplace_back(std::move(src), type, std::move(dst));
    if (jobs_.size() >= batch_size_) {
        eat_jobs(jobs_, batch_size_);
    }
}

// Returns: TRUE if the cancellable was cancelled, FALSE if it timed out.
static gboolean
cancellable_wait(GCancellable *cancellable, int cancellable_fd, gint timeout_ms)
{
    if (g_cancellable_is_cancelled(cancellable)) {
        return TRUE;
    }

    if (cancellable_fd == -1) {
        if (timeout_ms >= 0) {
            g_usleep(timeout_ms * 1000);
        }
        return g_cancellable_is_cancelled(cancellable);
    }

    GPollFD pfd;
    pfd.fd = cancellable_fd;
    pfd.events = G_IO_IN;
    pfd.revents = 0;

    int ret = g_poll(&pfd, 1, timeout_ms);

    if (ret > 0) {
        // cancel event
        if (pfd.revents & G_IO_IN) {
            return TRUE;
        }
    } else if (ret == 0) {
        // timeout
        ;
    } else {
        // error
        spdlog::warn("g_poll() returned an error: %s", g_strerror(errno));
        if (timeout_ms >= 0) {
            g_usleep(timeout_ms * 1000);
        }
    } 
    
    return g_cancellable_is_cancelled(cancellable);
}

void base_event_handler::timer_worker(int64_t interval) {
    int cancellable_fd = g_cancellable_get_fd(cancellable_);

    while(!cancellable_wait(cancellable_, cancellable_fd, (gint)interval)) {
        std::lock_guard<std::mutex> lock(jobs_mtx_);

        // trigger commit volatile index
        if (index_dirty_ && commit_volatile_index_timeout_ > 0)
            --commit_volatile_index_timeout_;
        if (commit_volatile_index_timeout_ == 0) {
            spdlog::debug("Push job: commit_volatile_index");
            jobs_.emplace_back("", anything::index_job_type::commit_volatile_index);
            commit_volatile_index_timeout_ = config_.commit_volatile_index_timeout;
            index_dirty_ = false;
        }

        if (!jobs_.empty())
            eat_jobs(jobs_, jobs_.size());
    }

    if (cancellable_fd != -1) {
        g_cancellable_release_fd(cancellable_);
    }
}

bool base_event_handler::scan_directory(const std::string& dir_path, std::function<bool(const std::string&)> handler) {
    spdlog::info("Scanning directory {}", dir_path);

    std::error_code ec;
    std::string path;

    try {
        // By default, symlinks are not followed
        std::filesystem::recursive_directory_iterator dirpos{ dir_path, std::filesystem::directory_options::skip_permission_denied };
        std::vector<std::string> blacklist_paths = get_blacklist_paths();
        for (auto it = begin(dirpos); it != end(dirpos); ++it) {
            path = std::move(it->path().string());
            if (is_path_in_blacklist(path, blacklist_paths) ||
                !std::filesystem::exists(it->path(), ec)) {
                    it.disable_recursion_pending();
                continue;
            }

            if (!handler(path)) {
                spdlog::error("Failed to handle path: {}", path);
                return false;
            }

            if (stop_scan_directory_) {
                spdlog::info("Scanning interrupted");
                return true;
            }
        }
    } catch (std::filesystem::filesystem_error const& e) {
        spdlog::error("Failed to scan directory: {}, {}, {}, {}",
            e.what(), e.path1().string(), e.path2().string(), e.code().value());
    } catch (const std::exception& e) {
        spdlog::error("Failed to scan directory {}: {}", dir_path, e.what());
    }

    spdlog::info("Scanning directory {} completed", dir_path);
    return true;
}

std::vector<std::string> base_event_handler::get_blacklist_paths()
{
    std::lock_guard<std::mutex> lock(config_access_mtx_);

    return config_.blacklist_paths;
}

void base_event_handler::set_blacklist_paths(const std::vector<std::string> &paths)
{
    std::lock_guard<std::mutex> lock(config_access_mtx_);

    config_.blacklist_paths = paths;
}
