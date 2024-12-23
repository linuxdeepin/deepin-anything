// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/base_event_handler.h"
#include "core/disk_scanner.h"

#include "common/file_record.hpp"
#include "utils/log.h"
#include "utils/string_helper.h"
#include <anythingadaptor.h>

base_event_handler::base_event_handler(std::string index_dir, QObject *parent)
    : QObject(parent), index_manager_(std::move(index_dir)), batch_size_(100),
      pool_((std::max)(std::thread::hardware_concurrency() - 3, 1U)),
      stop_timer_(false),
      timer_(std::thread(&base_event_handler::timer_worker, this, 1000)),
      delay_mode_(true/*index_manager_.indexed()*/) {
    new IAnythingAdaptor(this);
    QDBusConnection dbus = QDBusConnection::systemBus();
    if (!dbus.isConnected()) {
        qWarning() << "Failed to connect to system bus:" << dbus.lastError().message();
        exit(1);
    }
    QString service_name = "my.test.SAnything";
    QString object_name = "/my/test/OAnything";
    if (!dbus.interface()->isServiceRegistered(service_name)) {
        dbus.registerService(service_name);
        dbus.registerObject(object_name, this);
    }
}

base_event_handler::~base_event_handler() {
    pool_.wait_for_tasks();
    if (!jobs_.empty()) {
        // Eat all jobs
        for (auto&& job : jobs_) {
            eat_job(std::move(job));
        }
    }
}

void base_event_handler::terminate_processing() {
    stop_timer_ = true;
    anything::disk_scanner::stop_scanning = true;

    if (timer_.joinable()) {
        auto thread_id = timer_.get_id();
        timer_.join();
        std::ostringstream oss;
        oss << thread_id;
        spdlog::info("Timer thread {} has exited", oss.str());
    }
}

void base_event_handler::set_batch_size(std::size_t size) {
    batch_size_ = size;
}

bool base_event_handler::ignored_event(const std::string& path, bool ignored) {
    if (anything::string_helper::ends_with(path, ".longname"))
        return true; // 长文件名记录文件，直接忽略
    
    // 没有标记忽略前一条，则检查是否长文件目录
    if (!ignored) {
        // 向上找到一个当前文件的挂载点且匹配文件系统类型
        if (mnt_manager_.path_match_type(path, "fuse.dlnfs")) {
            // 长文件目录，标记此条事件被忽略
            return true;
        }
    }

    return false;
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
            // if (should_be_filtered(path)) {
            //     return;
            // }

            // index_manager_.add_index(std::move(path));
        }
    }
}

void base_event_handler::insert_index_directory(std::filesystem::path dir) {
    pool_.enqueue_detach([this, dir = std::move(dir)]() {
        this->insert_pending_paths(anything::disk_scanner::scan(dir));
    });
}

std::size_t base_event_handler::pending_paths_count() const {
    return pending_paths_.size();
}

void base_event_handler::refresh_mount_status() {
    mnt_manager_.update();
}

bool base_event_handler::device_available(unsigned int device_id) const {
    return mnt_manager_.contains_device(device_id);
}

std::string base_event_handler::fetch_mount_point_for_device(unsigned int device_id) const {
    return mnt_manager_.get_mount_point(device_id);
}

std::string base_event_handler::get_index_directory() const {
    return index_manager_.index_directory();
}

void base_event_handler::set_index_change_filter(
    std::function<bool(const std::string&)> filter) {
    index_change_filter_ = std::move(filter);
}

void base_event_handler::add_index_delay(std::string path) {
    jobs_push(std::move(path), anything::index_job_type::add);
    // if (should_be_filtered(path)) {
    //     return;
    // }
    // index_manager_.add_index(std::move(path));
}

void base_event_handler::remove_index_delay(std::string path) {
    jobs_push(std::move(path), anything::index_job_type::remove);
    // if (should_be_filtered(path)) {
    //     return;
    // }
    // index_manager_.remove_index(path);
}

void base_event_handler::update_index_delay(std::string src, std::string dst) {
    jobs_push(std::move(src), anything::index_job_type::update, std::move(dst));
}

bool base_event_handler::should_be_filtered(const std::string& path) const {
    if (index_change_filter_) {
        return std::invoke(index_change_filter_, path);
    }

    return false;
}

void base_event_handler::eat_jobs(std::vector<anything::index_job>& jobs, std::size_t number) {
    std::vector<anything::index_job> processing_jobs;
    processing_jobs.insert(
        processing_jobs.end(),
        std::make_move_iterator(jobs.begin()),
        std::make_move_iterator(jobs.begin() + number));
    jobs.erase(jobs.begin(), jobs.begin() + number);
    pool_.enqueue_detach([this, processing_jobs = std::move(processing_jobs)]() {
        for (const auto& job : processing_jobs) {
            eat_job(job);
        }
    });
}

void base_event_handler::eat_job(const anything::index_job& job) {
    if (job.type == anything::index_job_type::add) {
        index_manager_.add_index(job.src);
    } else if (job.type == anything::index_job_type::remove) {
        index_manager_.remove_index(job.src);
    } else if (job.type == anything::index_job_type::update) {
        if (job.dst) {
            index_manager_.update_index(job.src, *job.dst);
        }
    }
}

void base_event_handler::jobs_push(std::string src,
    anything::index_job_type type, std::optional<std::string> dst) {
    if (should_be_filtered(src) || (dst && should_be_filtered(*dst))) {
        return;
    }

    std::lock_guard<std::mutex> lock(jobs_mtx_);
    jobs_.emplace_back(std::move(src), type, std::move(dst));
    if (jobs_.size() >= batch_size_) {
        eat_jobs(jobs_, batch_size_);
    }
}

void base_event_handler::timer_worker(int64_t interval) {
    // When pending_batch_size is small, CPU usage is low, but total indexing time is longer.
    // When pending_batch_size is large, CPU usage is high, but total indexing time is shorter.
    constexpr std::size_t pending_batch_size = 2000;
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
        }

        // Automatically index missing system files to maintain index integrity when there are no jobs.
        if (idle) {
            std::vector<std::string> path_batch;
            {
                std::lock_guard<std::mutex> lock(pending_mtx_);
                if (!pending_paths_.empty()) {
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

            for (auto&& path : path_batch) {
                if (should_be_filtered(path)) {
                    continue;
                }

                // Before insertion, check if the file actually exists locally to avoid re-adding an index for a recently removed path.
                // - The document_exists check does not need to be real-time; it only needs to reflect the state at program startup to avoid
                //   efficiency issues caused by thread synchronization. 
                // - Since existing files without an index will not trigger new insertion events, only the initial state comparison is necessary.
                // - Index integrity is considered only for insertion here; deletions are not checked individually, as that would be inefficient.
                //   Instead, existence checks for indexed paths are handled at query time.
                if (!index_manager_.document_exists(path, true)) {
                    if (std::filesystem::exists(path)) {
                        add_index_delay(std::move(path));
                    }
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
    }
}

QStringList base_event_handler::search(const QString& path, const QString& keywords, int offset, int max_count) {
    if (offset < 0) {
        return {};
    }

    // Since there is no automatic cleanup for invalid indexes,
    // the system may contain paths that no longer exist.
    // To maintain index validity, invalid indexes are filtered and removed here.
    QStringList results = index_manager_.search(path, keywords, offset, max_count, true);
    int old_results_count = results.size();
    if (!results.empty()) {
        // anything::log::debug("result size: {}", results.size());
        // clean up index entries for non-existent files.
        std::vector<std::string> remove_list;
        for (int i = 0; i < results.size();) {
            std::string path = results[i].toStdString();
            if (!std::filesystem::exists(path)) {
                remove_list.push_back(std::move(path));
                results.removeAt(i);
            } else {
                ++i;
            }
        }

        // More results may exist; continue searching
        if (old_results_count == max_count) {
            if (!remove_list.empty()) {
                // anything::log::debug("remove size: {}", remove_list.size());
                results.append(search(path, keywords, offset + max_count, remove_list.size()));
            }
        }
        for (auto&& path : remove_list) {
            remove_index_delay(std::move(path));
        }
    }

    return results;
}

QStringList base_event_handler::search(const QString& keywords) {
    // Since there is no automatic cleanup for invalid indexes,
    // the system may contain paths that no longer exist.
    // To maintain index validity, invalid indexes are filtered and removed here.
    QStringList results = index_manager_.search(keywords, true);
    if (!results.empty()) {
        // clean up index entries for non-existent files.
        std::vector<std::string> remove_list;
        for (int i = 0; i < results.size();) {
            std::string path = results[i].toStdString();
            if (!std::filesystem::exists(path)) {
                remove_list.push_back(std::move(path));
                results.removeAt(i);
            } else {
                ++i;
            }
        }

        for (auto&& path : remove_list) {
            remove_index_delay(std::move(path));
        }
    }

    return results;
}

// No special handling for non-existent files; success is determined if the path does not exist.
bool base_event_handler::removePath(const QString& fullPath) {
    auto path = fullPath.toStdString();
    index_manager_.remove_index(path);
    return !index_manager_.document_exists(path);
}

bool base_event_handler::hasLFT(const QString& path) {
    return index_manager_.document_exists(path.toStdString());
}

void base_event_handler::addPath(const QString& fullPath) {
    auto path = fullPath.toStdString();
    if (std::filesystem::exists(path)) {
        add_index_delay(path);
    }
}

void base_event_handler::index_files_in_directory(const QString& directory_path) {
    insert_index_directory(directory_path.toStdString());
}

void base_event_handler::delay_indexing(bool delay) {
    delay_mode_ = delay;
}

QString base_event_handler::cache_directory() {
    return QString::fromStdString(index_manager_.index_directory());
}