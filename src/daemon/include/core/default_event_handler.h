// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_EVENT_HANDLER_H_
#define ANYTHING_EVENT_HANDLER_H_

#include <string>
#include <vector>
#include <glib.h>
#include "core/base_event_handler.h"
#include "core/mount_info.h"

ANYTHING_NAMESPACE_BEGIN

struct indexing_item {
    std::string origin_path;
    std::string event_path;
    bool different_path;
    bool enable;
};

struct fs_event_with_full_path {
    uint8_t     act;
    dev_t       device_id;
    std::string src;
    std::string dst;
};

class default_event_handler : public base_event_handler {
public:
    explicit default_event_handler(const event_handler_config &config);
    virtual ~default_event_handler();

    void handle(fs_event *event) override;

    void start_handle_init_scan(const std::string &path) override;

    void terminate_filter();

    bool handle_config_change(const std::string &key, const event_handler_config &new_config) override;

private:
    bool is_under_indexing_path(const std::string& path, indexing_item *&indexing_item);

    void convert_event_path_to_origin_path(std::string& path, const indexing_item& item);

    bool is_event_path_blocked(const std::string& path, indexing_item *&indexing_item);

    void filter_event(fs_event *event);

    bool convert_fs_event(fs_event *event, fs_event_with_full_path *event_with_full_path);

    static void* event_filter_thread_func(void* data);

    void handle_config_event();

    bool handle_blacklist_paths_change(const event_handler_config &old_config, const event_handler_config &new_config);

private:
    std::unordered_map<uint32_t, std::string> rename_from_;
    // config_ 使用情况
    // 1. 主线程中实例化时使用
    // 2. 主线程中调用 handle_config_change() 时使用
    // 3. 没有在 event_filter 线程中使用
    // 所以不存在多线程使用的情况
    event_handler_config config_;
    std::vector<indexing_item> indexing_items_;
    std::vector<std::string> event_path_blocked_list_;

    GAsyncQueue* event_queue_;
    GThread* event_filter_thread_;

    MountInfo *mount_info_;

    GAsyncQueue* config_event_queue_;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_EVENT_HANDLER_H_
