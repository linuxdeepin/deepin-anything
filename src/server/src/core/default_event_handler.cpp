// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/default_event_handler.h"

#include <cstdlib> // std::getenv
#include <glib.h>

#include "utils/log.h"
#include "utils/string_helper.h"
#include "vfs_change_consts.h"
#include "core/config.h"
#include "utils/tools.h"

ANYTHING_NAMESPACE_BEGIN

// /data 和非 data 需要保持一致，最好有一种方式能够获取当前的状态
default_event_handler::default_event_handler(std::shared_ptr<event_handler_config> config)
    : base_event_handler(config), config_(config) {
    // init indexing_items_
    spdlog::debug("processing indexing_paths...");
    for (auto& origin_path : config_->indexing_paths) {
        if (!std::filesystem::exists(origin_path)) {
            continue;
        }

        indexing_item item = {
            .origin_path = origin_path,
            .event_path = "",
            .different_path = false,
        };

        char *event_path = get_full_path(origin_path.c_str());
        if (event_path == nullptr) {
            spdlog::error("Failed to get event path: {}", origin_path);
            continue;
        }
        item.event_path = std::string(event_path) + "/";
        g_free(event_path);
        spdlog::debug("Determine the event path: {} -> {}", origin_path, item.event_path);

        item.different_path = item.origin_path != item.event_path;
        item.origin_path += "/";

        indexing_items_.emplace_back(item);
    }

    // init event_path_blocked_list_
    spdlog::debug("processing blacklist_paths...");
    for (auto& path : config_->blacklist_paths) {
        if (!std::filesystem::exists(path)) {
            event_path_blocked_list_.emplace_back(path);
        } else {
            char *event_path = get_full_path(path.c_str());
            if (event_path == nullptr) {
                spdlog::error("Failed to get event path: {}", path);
                continue;
            }
            event_path_blocked_list_.emplace_back(std::string(event_path));
            g_free(event_path);
            spdlog::debug("Determine the event path: {} -> {}", path, event_path_blocked_list_.back());
        }
    }

    // scan the indexing_items_
    for (auto& item : indexing_items_) {
        add_index_delay(item.origin_path);
        insert_index_directory(item.origin_path);
    }

    // Initialize mount cache
    refresh_mount_status();

    // index_directory 必须设置为完整路径
    set_index_change_filter([this](const std::string& path) {
        auto index_directory = get_index_directory();
        auto names = string_helper::split(path, "/");
        using string_helper::starts_with;
        return starts_with(path, index_directory) ||
               (starts_with(index_directory, "/") && starts_with(path, "/data" + index_directory)) /*||
               std::any_of(names.begin(), names.end(), [](const std::string& name) { return starts_with(name, "."); })*/;
    });

    spdlog::debug("cache directory: {}", get_index_directory());
}

bool default_event_handler::is_under_indexing_path(const std::string& path, indexing_item *&indexing_item) {
    for (auto& item : indexing_items_) {
        if (path.rfind(item.event_path) == 0) {
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

void default_event_handler::handle(fs_event event) {
    [[maybe_unused]] const char* act_names[] = {"file_created", "link_created", "symlink_created", "dir_created", "file_deleted", "dir_deleted", "file_renamed", "dir_renamed"};

    // Update partition event
    if (event.act == ACT_MOUNT || event.act == ACT_UNMOUNT) {
        spdlog::info("{}: {}", (event.act == ACT_MOUNT ? "Mount a device" : "Unmount a device"), event.src);
        refresh_mount_status();
        return;
    }

    std::string root;
    if (event.act < ACT_MOUNT) {
        unsigned int device_id = MKDEV(event.major, event.minor);
        if (!device_available(device_id)) {
            spdlog::warn("Unknown device: {}, dev: {}:{}, path: {}, cookie: {}",
                +event.act, event.major, +event.minor, event.src, event.cookie);
            return;
        }

        root = fetch_mount_point_for_device(device_id);
        if (root == "/")
            root.clear();
    }

    // spdlog::debug("Last Action: {}, src: {}", +event.act, event.src);

    switch (event.act) {
    case ACT_NEW_FILE:
    case ACT_NEW_SYMLINK:
    case ACT_NEW_LINK:
    case ACT_NEW_FOLDER:
    case ACT_DEL_FILE:
    case ACT_DEL_FOLDER:
        // Keeps the dst empty.
        break;
    case ACT_RENAME_FROM_FILE:
    case ACT_RENAME_FROM_FOLDER:
        rename_from_.emplace(event.cookie, event.src);
        return;
    case ACT_RENAME_TO_FILE:
    case ACT_RENAME_TO_FOLDER:
        if (auto search = rename_from_.find(event.cookie);
            search != rename_from_.end()) {
            event.act = event.act == ACT_RENAME_TO_FILE ? ACT_RENAME_FILE : ACT_RENAME_FOLDER;
            event.dst = event.src;
            event.src = rename_from_[event.cookie];
        }
        break;
    case ACT_RENAME_FILE:
    case ACT_RENAME_FOLDER:
        spdlog::warn("Don't support file action: {}", +event.act);
        return;
    default:
        spdlog::warn("Unknown file action: {}", +event.act);
        return;
    }

    if (!root.empty()) {
        event.src = root + event.src;
        if (!event.dst.empty())
            event.dst = root + event.dst;
    }

    if (event.act == ACT_RENAME_FILE || event.act == ACT_RENAME_FOLDER) {
        rename_from_.erase(event.cookie);
    }

    // spdlog::debug("Received a(an) {} message(src={},dst={})", act_names[event.act], event.src, event.dst);

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
            }
            size_t event_src_len = event.src.length();
            QString oldPath = QString::fromStdString(event.src);
            for (auto const& result : traverse_directory(oldPath)) {
                std::string src = result.toStdString();
                if (isDstBlocked) {
                    remove_index_delay(std::move(src));
                } else {
                    std::string dst = src;
                    dst.replace(0, event_src_len, event.dst);
                    update_index_delay(std::move(src), std::move(dst));
                }
            }
        }
    }
}

ANYTHING_NAMESPACE_END
