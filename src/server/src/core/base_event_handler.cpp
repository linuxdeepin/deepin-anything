// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/base_event_handler.h"

#include "core/disk_scanner.h"
#include "common/file_record.h"
#include "utils/log.h"
#include "utils/string_helper.h"
#include <anythingadaptor.h>

#define COMMIT_VOLATILE_INDEX_TIMEOUT 10
#define COMMIT_PERSISTENT_INDEX_TIMEOUT 600

base_event_handler::base_event_handler(std::string persistent_index_dir, std::string volatile_index_dir, QObject *parent)
    : QObject(parent), index_manager_(std::move(persistent_index_dir), std::move(volatile_index_dir)), batch_size_(200),
      pool_((std::max)(std::thread::hardware_concurrency() - 3, 1U)),
      stop_timer_(false),
      timer_(std::thread(&base_event_handler::timer_worker, this, 1000)),
      delay_mode_(true/*index_manager_.indexed()*/),
      index_dirty_(false),
      volatile_index_dirty_(false),
      commit_volatile_index_timeout_(COMMIT_VOLATILE_INDEX_TIMEOUT),
      commit_persistent_index_timeout_(COMMIT_PERSISTENT_INDEX_TIMEOUT) {
    new AnythingAdaptor(this);
    QDBusConnection dbus = QDBusConnection::sessionBus();
    if (!dbus.isConnected()) {
        spdlog::info("Failed to connect to system bus: {}", dbus.lastError().message().toStdString());
        exit(1);
    }
    QString service_name = "com.deepin.anything";
    QString object_name = "/com/deepin/anything";
    if (!dbus.interface()->isServiceRegistered(service_name)) {
        spdlog::info("Registering service: {}", service_name.toStdString());
        dbus.registerService(service_name);
        dbus.registerObject(object_name, this);
    } else {
        quint32 pid = 0;
        QString pidStr;
        QDBusReply<quint32> reply = dbus.interface()->servicePid(service_name);
        if (reply.isValid())
            pid = reply.value();
        pidStr.setNum(pid);
        spdlog::info("Registered service: {}, {}", service_name.toStdString(), pidStr.toStdString());
    }

    index_dirty_ = index_manager_.refresh_indexes();
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

void base_event_handler::scan_index_delay(std::string path) {
    jobs_push(std::move(path), anything::index_job_type::scan);
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
    } else if (job.type == anything::index_job_type::scan) {
        for (auto&& path : anything::disk_scanner::scan(job.src)) {
            index_manager_.add_index(std::move(path));
        }
    }
}

void base_event_handler::jobs_push(std::string src,
    anything::index_job_type type, std::optional<std::string> dst) {
    // spdlog::debug("jobs_push: src({}), dst({})", src, dst ? *dst : "");
    if (should_be_filtered(src) || (dst && should_be_filtered(*dst))) {
        return;
    }

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
            if (commit_volatile_index_timeout_ == 0 && jobs_.empty()) {
                index_manager_.commit();
                commit_volatile_index_timeout_ = 10;
                index_dirty_ = false;
                volatile_index_dirty_ = true;
            }

            // Commit persistent index
            if (volatile_index_dirty_ && commit_persistent_index_timeout_ > 0)
                --commit_persistent_index_timeout_;
            if (commit_persistent_index_timeout_ == 0 && jobs_.empty()) {
                index_manager_.persist_index();
                commit_persistent_index_timeout_ = 600;
                volatile_index_dirty_ = false;
            }
        }

        // Automatically index missing system files to maintain index integrity when there are no jobs.
        if (idle) {
            bool pending_paths_empty = false;
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
                    pending_paths_empty = pending_paths_.empty();
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

            if (pending_paths_empty) {
                spdlog::info("Pending paths are empty, trigger index commit");
                index_manager_.commit();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
    }
}

QStringList base_event_handler::search(const QString& path,
    QString keywords, int offset, int max_count) {
    if (offset < 0) {
        return {};
    }

    return index_manager_.search(path, keywords, offset, max_count, true);
}

QStringList base_event_handler::search(const QString& path, QString keywords) {
    return index_manager_.search(path, keywords, true);
}

QStringList base_event_handler::search(const QString& path, QString keywords, const QString& type) {
    return index_manager_.search(path, keywords, type, true);
}

QStringList base_event_handler::search(const QString& path,
    QString keywords, const QString& after, const QString& before) {
    return index_manager_.search(path, keywords, after, before, true);
}

QStringList base_event_handler::traverse_directory(const QString& path) {
    return index_manager_.traverse_directory(path, true);
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

QStringList base_event_handler::hasLFTSubdirectories(QString path) const {
    (void)path;
    return {};
}

void base_event_handler::addPath(const QString &fullPath) {
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

QStringList base_event_handler::search(
    int maxCount, qint64 icase, quint32 startOffset,
    quint32 endOffset, const QString& path,
    QString keyword, bool useRegExp,
    quint32& startOffsetReturn, quint32& endOffsetReturn) {
    QString prefix = "\\A(?:[^/]*";
    QString suffix = "[^/]*)\\z";
    // spdlog::info("Original path: {}, keyword:{}, maxCount: {}", path.toStdString(), keyword.toStdString(), maxCount);
    if (keyword.startsWith(prefix) && keyword.endsWith(suffix)) {
        keyword.remove(0, prefix.length());
        keyword.remove(keyword.length() - suffix.length(), suffix.length());
    }
    auto result = search(path, keyword, startOffset, maxCount);
    (void)icase;
    (void)endOffset;
    (void)useRegExp;
    startOffsetReturn = startOffset + 1;
    endOffsetReturn = result.size() == maxCount
                    ? startOffsetReturn + 1
                    : startOffsetReturn;
    return result;
}

QStringList base_event_handler::parallelsearch(
    const QString& path, quint32 startOffset,
    quint32 endOffset, const QString& keyword,
    const QStringList& rules, quint32& startOffsetReturn,
    quint32& endOffsetReturn) {
    // if (!rules.isEmpty()) {
    //     QString firstElement = rules.at(0);
    //     qDebug() << "First element:" << firstElement;
    // } else {
    //     qDebug() << "List is empty!";
    // }
    (void)rules;
    return search(100, 0, startOffset, endOffset, path, keyword, true, startOffsetReturn, endOffsetReturn);
}

void base_event_handler::async_search(QString keywords)
{
    index_manager_.async_search(keywords, true, [this](const QStringList& results) {
        if (!results.isEmpty()) {
            Q_EMIT asyncSearchCompleted(results);
        }
    });
}
