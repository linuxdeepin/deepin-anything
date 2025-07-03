// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_BASE_EVENT_HANDLER_H_
#define ANYTHING_BASE_EVENT_HANDLER_H_

#include <filesystem>
#include <glib.h>

#include "common/anything_fwd.hpp"
#include "common/fs_event.h"
#include "core/file_index_manager.h"
#include "core/mount_manager.h"
#include "core/thread_pool.h"
#include "core/config.h"

ANYTHING_NAMESPACE_BEGIN

enum class index_job_type : char {
    add, remove, update, scan, recursive_update
};

struct index_job {
    std::string src;
    std::optional<std::string> dst;
    index_job_type type;

    index_job(std::string src, index_job_type type, std::optional<std::string> dst = std::nullopt)
        : src(std::move(src)), dst(std::move(dst)), type(type) {}
};

ANYTHING_NAMESPACE_END

class base_event_handler
{
public:
    base_event_handler(std::shared_ptr<event_handler_config> config);
    virtual ~base_event_handler();

    virtual void handle(anything::fs_event *event) = 0;

    void terminate_processing();

    void notify_config_changed();

protected:
    void set_batch_size(std::size_t size);

    bool ignored_event(const std::string& path, bool ignored);

    void insert_pending_paths(std::vector<std::string> paths);
    void insert_index_directory(const std::string &dir);
    void set_index_dirs(const std::vector<std::string> &paths);

    std::size_t pending_paths_count() const;

    void refresh_mount_status();

    bool device_available(unsigned int device_id) const;

    std::string fetch_mount_point_for_device(unsigned int device_id) const;

    std::string get_index_directory() const;

    void add_index_delay(std::string path);
    void remove_index_delay(std::string path);
    void update_index_delay(std::string src, std::string dst);
    void scan_index_delay(std::string path);
    void recursive_update_index_delay(std::string src, std::string dst);

    std::vector<std::string> traverse_directory(const std::string& path);

private:
    void eat_jobs(std::vector<anything::index_job>& jobs, std::size_t number);
    void eat_job(const anything::index_job& job);

    void jobs_push(std::string src, anything::index_job_type type, std::optional<std::string> dst = std::nullopt);

    void timer_worker(int64_t interval);

private:
    std::shared_ptr<event_handler_config> config_;
    anything::mount_manager mnt_manager_;
    anything::file_index_manager index_manager_;
    std::size_t batch_size_;
    std::vector<std::string> pending_paths_;
    std::vector<anything::index_job> jobs_;
    anything::thread_pool pool_;
    std::atomic<bool> stop_timer_;
    std::mutex jobs_mtx_;
    std::mutex pending_mtx_;
    std::thread timer_;
    bool delay_mode_;

    bool index_dirty_;
    bool volatile_index_dirty_;
    int commit_volatile_index_timeout_;
    int commit_persistent_index_timeout_;

    anything::index_status index_status_;

    std::vector<std::string> index_dirs_;
    std::mutex index_dirs_mtx_;

    gint event_process_thread_count_;
};

#endif // ANYTHING_BASE_EVENT_HANDLER_H_
