// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/base_event_handler.h"

#include "core/disk_scanner.h"
#include "utils/log.h"
#include "utils/string_helper.h"
#include "utils/tools.h"

#include <QCoreApplication>

base_event_handler::base_event_handler(std::shared_ptr<event_handler_config> config)
    : config_(config),
      index_manager_(config->persistent_index_dir, config->volatile_index_dir, config->file_type_mapping),
      batch_size_(200),
      pool_(1),
      stop_timer_(false),
      delay_mode_(true/*index_manager_.indexed()*/),
      index_dirty_(false),
      volatile_index_dirty_(false),
      commit_volatile_index_timeout_(config->commit_volatile_index_timeout),
      commit_persistent_index_timeout_(config->commit_persistent_index_timeout),
      index_status_(anything::index_status::loading),
      event_process_thread_count_(0),
      stop_scan_directory_(false) {
    index_dirty_ = index_manager_.refresh_indexes(config_->blacklist_paths);

    // The timer thread is started only after all initialization is completed
    timer_ = std::thread(&base_event_handler::timer_worker, this, 1000);
}

base_event_handler::~base_event_handler() {
}

void base_event_handler::terminate_processing() {
    stop_timer_ = true;
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
        for (auto&& job : jobs_) {
            eat_job(std::move(job));
        }
    }
}

void base_event_handler::set_index_invalid_and_restart() {
    spdlog::info("Set index invalid and restart");

    index_manager_.set_index_invalid();

    set_app_restart(true);
    qApp->quit();
}

void base_event_handler::set_batch_size(std::size_t size) {
    batch_size_ = size;
}

void base_event_handler::insert_pending_paths(
    std::vector<std::string> paths) {
    if (delay_mode_) {
        std::lock_guard<std::mutex> lock(pending_mtx_);
        pending_paths_.insert(pending_paths_.end(),
            std::make_move_iterator(paths.begin()),
            std::make_move_iterator(paths.end()));
    } else {
        for (auto&& path : paths) {
            add_index_delay(std::move(path));
        }
    }
}

void base_event_handler::insert_index_directory(const std::string &dir) {
    pool_.enqueue_detach([this, dir]() {
        this->insert_pending_paths(anything::disk_scanner::scan(dir, config_->blacklist_paths));

        {
            std::lock_guard<std::mutex> lock(index_dirs_mtx_);
            index_dirs_.erase(std::find(index_dirs_.begin(), index_dirs_.end(), dir));
            if (index_dirs_.empty()) {
                index_status_ = anything::index_status::scanning;
            }
        }
    });
}

void base_event_handler::set_index_dirs(const std::vector<std::string> &paths) {
    std::lock_guard<std::mutex> lock(index_dirs_mtx_);
    index_dirs_ = paths;
    for (auto&& path : index_dirs_) {
        add_index_delay(path);
        insert_index_directory(path);
    }
}

std::size_t base_event_handler::pending_paths_count() const {
    return pending_paths_.size();
}

std::string base_event_handler::get_index_directory() const {
    return index_manager_.index_directory();
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

void base_event_handler::eat_jobs(std::vector<anything::index_job>& jobs, std::size_t number) {
    std::vector<anything::index_job> processing_jobs;
    processing_jobs.insert(
        processing_jobs.end(),
        std::make_move_iterator(jobs.begin()),
        std::make_move_iterator(jobs.begin() + number));
    jobs.erase(jobs.begin(), jobs.begin() + number);
    pool_.enqueue_detach([this, processing_jobs = std::move(processing_jobs)]() {
        g_atomic_int_inc(&this->event_process_thread_count_);
        for (const auto& job : processing_jobs) {
            eat_job(job);
        }
        g_atomic_int_dec_and_test(&this->event_process_thread_count_);
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
                spdlog::info("Index scan completed");
                // index commit will be triggered by timer
                ret = true;
            }
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

void base_event_handler::jobs_push(std::string src,
    anything::index_job_type type, std::optional<std::string> dst) {

    std::lock_guard<std::mutex> lock(jobs_mtx_);
    index_dirty_ = true;
    jobs_.emplace_back(std::move(src), type, std::move(dst));
    if (jobs_.size() >= batch_size_) {
        eat_jobs(jobs_, batch_size_);
    }
}

void base_event_handler::timer_worker(int64_t interval) {
    // When pending_batch_size is small, CPU usage is low, but total indexing time is longer.
    // When pending_batch_size is large, CPU usage is high, but total indexing time is shorter.
    constexpr std::size_t pending_batch_size = 20000;
    while(!stop_timer_) {
        // {
        //     std::lock_guard<std::mutex> lock(jobs_mtx_);
        //     if (!jobs_.empty()) {
        //         eat_jobs(jobs_, std::min(batch_size_, jobs_.size()));
        //     }
        // }
        // std::this_thread::sleep_for(std::chrono::milliseconds(interval));

        bool idle = false;
        {
            std::lock_guard<std::mutex> lock(jobs_mtx_);
            if (!jobs_.empty()) {
                eat_jobs(jobs_, std::min(batch_size_, jobs_.size()));
            } else {
                idle = true;
            }

            // Commit volatile index
            if (index_dirty_ && commit_volatile_index_timeout_ > 0)
                --commit_volatile_index_timeout_;
            if (commit_volatile_index_timeout_ == 0 && jobs_.empty() && !pool_.busy() &&
                g_atomic_int_get(&event_process_thread_count_) == 0) {
                if (!index_manager_.commit(index_status_)) {
                    spdlog::info("Failed to commit index");
                    set_index_invalid_and_restart();
                }
                commit_volatile_index_timeout_ = config_->commit_volatile_index_timeout;
                index_dirty_ = false;
                volatile_index_dirty_ = true;
            }

            // Commit persistent index
            if (volatile_index_dirty_ && commit_persistent_index_timeout_ > 0)
                --commit_persistent_index_timeout_;
            if (commit_persistent_index_timeout_ == 0 && jobs_.empty() && !pool_.busy() &&
                g_atomic_int_get(&event_process_thread_count_) == 0) {
                index_manager_.persist_index();
                commit_persistent_index_timeout_ = config_->commit_persistent_index_timeout;
                volatile_index_dirty_ = false;
            }
        }

        // Automatically index missing system files to maintain index integrity when there are no jobs.
        bool pending_paths_empty = false;
        if (idle) {
            std::vector<std::string> path_batch;
            {
                std::lock_guard<std::mutex> lock(pending_mtx_);
                pending_paths_empty = pending_paths_.empty();
                if (!pending_paths_empty) {
                    std::size_t batch_size = std::min(pending_batch_size, pending_paths_.size());
                    path_batch.insert(
                        path_batch.end(),
                        std::make_move_iterator(pending_paths_.begin()),
                        std::make_move_iterator(pending_paths_.begin() + batch_size));
                    pending_paths_.erase(pending_paths_.begin(), pending_paths_.begin() + batch_size);
                }
            }

            if (path_batch.size() > 0) {
                spdlog::debug("path batch size: {}", path_batch.size());
            }

            try {
                for (auto&& path : path_batch) {
                    // Before insertion, check if the file actually exists locally to avoid re-adding an index for a recently removed path.
                    // - The document_exists check does not need to be real-time; it only needs to reflect the state at program startup to avoid
                    //   efficiency issues caused by thread synchronization. 
                    // - Since existing files without an index will not trigger new insertion events, only the initial state comparison is necessary.
                    // - Index integrity is considered only for insertion here; deletions are not checked individually, as that would be inefficient.
                    //   Instead, existence checks for indexed paths are handled at query time.
                    if (!index_manager_.document_exists(path, true)) {
                        std::error_code ec;
                        if (std::filesystem::exists(path, ec)) {
                            add_index_delay(std::move(path));
                        }
                    }
                }
            } catch (const std::exception& e) {
                spdlog::error("Failed to add index in timer worker: {}", e.what());
            }
        }

        if (index_status_ == anything::index_status::scanning &&
            pending_paths_empty &&
            idle &&
            !pool_.busy() &&
            g_atomic_int_get(&event_process_thread_count_) == 0) {
            spdlog::info("Index scan completed, trigger index commit");

            index_status_ = anything::index_status::monitoring;
            if (!index_manager_.commit(index_status_)) {
                spdlog::info("Failed to commit index");
                set_index_invalid_and_restart();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
    }
}

bool base_event_handler::scan_directory(const std::string& dir_path, std::function<bool(const std::string&)> handler) {
    spdlog::info("Scanning directory {}", dir_path);

    std::error_code ec;
    std::string path;
    // By default, symlinks are not followed
    std::filesystem::recursive_directory_iterator dirpos{ dir_path, std::filesystem::directory_options::skip_permission_denied };
    for (auto it = begin(dirpos); it != end(dirpos); ++it) {
        path = std::move(it->path().string());
        if (is_path_in_blacklist(path, config_->blacklist_paths) ||
            !std::filesystem::exists(it->path(), ec)) {
                it.disable_recursion_pending();
            continue;
        }

        if (!handler(path)) {
            return false;
        }

        if (stop_scan_directory_) {
            spdlog::info("Scanning interrupted");
            return true;
        }
    }

    spdlog::info("Scanning directory {} completed", dir_path);
    return true;
}
