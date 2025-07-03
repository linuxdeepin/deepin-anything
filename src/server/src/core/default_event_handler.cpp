// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/default_event_handler.h"

#include <cstdlib> // std::getenv
#include <glib.h>
#include <gmodule.h>

#include "utils/log.h"
#include "utils/string_helper.h"
#include "vfs_change_consts.h"
#include "core/config.h"
#include "utils/tools.h"
#include "utils/string_helper.h"

ANYTHING_NAMESPACE_BEGIN

#define ACT_TERMINATE 100

// 检查 event_path 与 indexing_items_ 中的 event_path 是否冲突
bool is_event_path_conflict_with_indexing_items(const std::string& event_path,
                                                const std::vector<indexing_item>& indexing_items) {
    for (auto& item : indexing_items) {
        if (string_helper::starts_with(event_path, item.event_path) ||
            string_helper::starts_with(item.event_path, event_path)) {
            return true;
        }
    }
    return false;
}

std::string get_event_path(const std::string& origin_path, const std::vector<indexing_item>& indexing_items) {
    std::error_code ec;
    if (!std::filesystem::exists(origin_path, ec)) {
        spdlog::error("The origin path {} does not exist: {}", origin_path, ec.message());
        return "";
    }

    std::string event_path_str;
    char *event_path = get_full_path(origin_path.c_str());
    if (event_path == nullptr) {
        spdlog::warn("Failed to get event path, use the origin path: {}", origin_path);
        event_path_str = origin_path;
    } else {
        event_path_str = std::string(event_path);
        g_free(event_path);
    }

    // add "/" to the end of the event_path for simplify the comparison
    std::string event_path_with_slash = event_path_str;
    if (!string_helper::ends_with(event_path_with_slash, "/")) {
        event_path_with_slash += "/";
    }

    if (is_event_path_conflict_with_indexing_items(event_path_with_slash, indexing_items)) {
        spdlog::warn("Event path {} conflicts with already configured event paths, skip", event_path_with_slash);
        return "";
    }

    return event_path_with_slash;
}

// /data 和非 data 需要保持一致，最好有一种方式能够获取当前的状态
default_event_handler::default_event_handler(std::shared_ptr<event_handler_config> config)
    : base_event_handler(config), config_(config) {
    event_queue_ = g_async_queue_new();
    event_filter_thread_ = g_thread_new("event_filter", event_filter_thread_func, this);

    // init indexing_items_
    spdlog::info("processing indexing_paths...");
    for (auto& origin_path : config_->indexing_paths) {
        std::string event_path_with_slash = get_event_path(origin_path, indexing_items_);
        if (event_path_with_slash.empty()) {
            continue;
        }

        std::string origin_path_with_slash = origin_path;
        if (!string_helper::ends_with(origin_path_with_slash, "/")) {
            origin_path_with_slash += "/";
        }
        indexing_item item = {
            .origin_path = origin_path_with_slash,
            .event_path = event_path_with_slash,
            .different_path = origin_path_with_slash != event_path_with_slash,
        };
        spdlog::info("Determine the event path: {} -> {}", item.origin_path, item.event_path);

        indexing_items_.emplace_back(item);
    }

    // init event_path_blocked_list_
    spdlog::info("processing blacklist_paths...");
    for (auto& path : config_->blacklist_paths) {
        std::error_code ec;
        if (!anything::string_helper::starts_with(path, "/") || !std::filesystem::exists(path, ec)) {
            event_path_blocked_list_.emplace_back(path);
        } else {
            char *event_path = get_full_path(path.c_str());
            if (event_path == nullptr) {
                spdlog::error("Failed to get event path: {}", path);
                continue;
            }
            event_path_blocked_list_.emplace_back(std::string(event_path));
            g_free(event_path);
            spdlog::info("Determine the event path: {} -> {}", path, event_path_blocked_list_.back());
        }
    }

    // scan the indexing_items_
    std::vector<std::string> indexing_paths;
    for (auto& item : indexing_items_) {
        indexing_paths.emplace_back(item.origin_path);
        // remove the last "/"
        if (indexing_paths.back() != "/") {
            indexing_paths.back().pop_back();
        }
    }
    set_index_dirs(indexing_paths);

    // Initialize mount cache
    refresh_mount_status();
}

bool default_event_handler::is_under_indexing_path(const std::string& path, indexing_item *&indexing_item) {
    for (auto& item : indexing_items_) {
        if (string_helper::starts_with(path, item.event_path)) {
            indexing_item = &item;
            return true;
        }
    }
    return false;
}

// 将路径中的 /persistent/home/user 替换为 /home/user
void default_event_handler::convert_event_path_to_origin_path(std::string& path, const indexing_item& item) {
    if (item.different_path) {
        path.replace(0, item.event_path.length(), item.origin_path);
    }
}

bool default_event_handler::is_event_path_blocked(const std::string& path, indexing_item *&indexing_item) {
    return !is_under_indexing_path(path, indexing_item) || is_path_in_blacklist(path, event_path_blocked_list_);
}

void default_event_handler::handle(fs_event *event) {
    g_async_queue_push(event_queue_, event);
}

// 将 fs_event 转换为 fs_event_with_full_path
// return true 表示处理完成，false 表示需要继续处理
bool default_event_handler::convert_fs_event(fs_event *event, fs_event_with_full_path *event_with_full_path) {
    // Update partition event
    if (event->act == ACT_MOUNT || event->act == ACT_UNMOUNT) {
        spdlog::info("{}: {}", (event->act == ACT_MOUNT ? "Mount a device" : "Unmount a device"), event->src);
        refresh_mount_status();
        return true;
    }

    std::string root;
    if (event->act < ACT_MOUNT) {
        unsigned int device_id = MKDEV(event->major, event->minor);
        if (!device_available(device_id)) {
            spdlog::warn("Unknown device: {}, dev: {}:{}, path: {}, cookie: {}",
                +event->act, event->major, +event->minor, event->src, event->cookie);
            return true;
        }

        root = fetch_mount_point_for_device(device_id);
        if (root == "/")
            root.clear();
    }

    switch (event->act) {
    case ACT_NEW_FILE:
    case ACT_NEW_SYMLINK:
    case ACT_NEW_LINK:
    case ACT_NEW_FOLDER:
    case ACT_DEL_FILE:
    case ACT_DEL_FOLDER:
        {
            event_with_full_path->act = event->act;
            event_with_full_path->src = event->src;
            event_with_full_path->dst = "";
        }
        break;
    case ACT_RENAME_FROM_FILE:
    case ACT_RENAME_FROM_FOLDER:
        rename_from_.emplace(event->cookie, event->src);
        return true;
    case ACT_RENAME_TO_FILE:
    case ACT_RENAME_TO_FOLDER:
        if (auto search = rename_from_.find(event->cookie);
            search != rename_from_.end()) {
            event_with_full_path->act = event->act == ACT_RENAME_TO_FILE ? ACT_RENAME_FILE : ACT_RENAME_FOLDER;
            event_with_full_path->dst = event->src;
            event_with_full_path->src = rename_from_[event->cookie];
        }
        break;
    case ACT_RENAME_FILE:
    case ACT_RENAME_FOLDER:
        spdlog::warn("Don't support file action: {}", +event->act);
        return true;
    default:
        spdlog::warn("Unknown file action: {}", +event->act);
        return true;
    }

    if (!root.empty()) {
        event_with_full_path->src = root + event_with_full_path->src;
        if (!event_with_full_path->dst.empty())
            event_with_full_path->dst = root + event_with_full_path->dst;
    }

    if (event->act == ACT_RENAME_FILE || event->act == ACT_RENAME_FOLDER) {
        rename_from_.erase(event->cookie);
    }

    return false;
}

void default_event_handler::filter_event(fs_event *fs_event) {
    [[maybe_unused]] const char* act_names[] = {"file_created", "link_created", "symlink_created", "dir_created", "file_deleted", "dir_deleted", "file_renamed", "dir_renamed"};

    fs_event_with_full_path event;
    if (convert_fs_event(fs_event, &event)) {
        return;
    }

    spdlog::debug("Received event: {} {} {}", act_names[event.act], event.src, event.dst);

    // Preparations are done, starting to process the event.

    indexing_item *src_indexing_item = nullptr;
    indexing_item *dst_indexing_item = nullptr;
    if (event.act != ACT_RENAME_FILE &&
        event.act != ACT_RENAME_FOLDER &&
        is_event_path_blocked(event.src, src_indexing_item)) {
        return;
    }

    bool ignored = false;
    ignored = ignored_event(event.dst.empty() ? event.src : event.dst, ignored);
    if (!ignored) {
        if (event.act == ACT_NEW_FILE || event.act == ACT_NEW_SYMLINK ||
            event.act == ACT_NEW_LINK || event.act == ACT_NEW_FOLDER) {
            // Do not check for the existence of files; we trust the kernel module.
            convert_event_path_to_origin_path(event.src, *src_indexing_item);
            add_index_delay(std::move(event.src));
        } else if (event.act == ACT_DEL_FILE || event.act == ACT_DEL_FOLDER) {
            convert_event_path_to_origin_path(event.src, *src_indexing_item);
            remove_index_delay(std::move(event.src));
        } else if (event.act == ACT_RENAME_FILE) {
            bool isSrcBlocked = is_event_path_blocked(event.src, src_indexing_item);
            bool isDstBlocked = is_event_path_blocked(event.dst, dst_indexing_item);

            if (isSrcBlocked && isDstBlocked) {
                return;
            } else if (isSrcBlocked) {
                convert_event_path_to_origin_path(event.dst, *dst_indexing_item);
                add_index_delay(std::move(event.dst));
            } else if (isDstBlocked) {
                convert_event_path_to_origin_path(event.src, *src_indexing_item);
                remove_index_delay(std::move(event.src));
            } else {
                convert_event_path_to_origin_path(event.src, *src_indexing_item);
                convert_event_path_to_origin_path(event.dst, *dst_indexing_item);
                update_index_delay(std::move(event.src), std::move(event.dst));
            }
        } else if (event.act == ACT_RENAME_FOLDER) {
            // Rename all files/folders in this folder(including this folder)
            bool isSrcBlocked = is_event_path_blocked(event.src, src_indexing_item);
            bool isDstBlocked = is_event_path_blocked(event.dst, dst_indexing_item);

            if (isSrcBlocked && isDstBlocked) {
                return;
            }

            if (isSrcBlocked) {
                convert_event_path_to_origin_path(event.dst, *dst_indexing_item);
                add_index_delay(event.dst);
                scan_index_delay(std::move(event.dst));
                return;
            }

            convert_event_path_to_origin_path(event.src, *src_indexing_item);
            if (!isDstBlocked) {
                convert_event_path_to_origin_path(event.dst, *dst_indexing_item);
            } else {
                event.dst.clear();
            }
            recursive_update_index_delay(std::move(event.src), std::move(event.dst));
        }
    }
}

void default_event_handler::terminate_filter() {
    fs_event* event = g_slice_new(fs_event);
    event->act = ACT_TERMINATE;
    g_async_queue_push(event_queue_, event);
    g_thread_join(event_filter_thread_);
    spdlog::info("Event filter thread terminated");
    g_async_queue_unref(event_queue_);
}

void* default_event_handler::event_filter_thread_func(void* data) {
    auto handler = static_cast<default_event_handler*>(data);
    while (true) {
        fs_event* event = (fs_event*)g_async_queue_pop(handler->event_queue_);
        if (event->act == ACT_TERMINATE) {
            g_slice_free(fs_event, event);
            break;
        }
        handler->filter_event(event);
        g_slice_free(fs_event, event);
    }

    return NULL;
}

ANYTHING_NAMESPACE_END
