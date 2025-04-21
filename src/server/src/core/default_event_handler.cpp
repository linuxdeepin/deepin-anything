// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/default_event_handler.h"

#include <cstdlib> // std::getenv

#include "utils/log.h"
#include "utils/print_helper.h"
#include "utils/string_helper.h"
#include "vfs_change_consts.h"
#include "core/config.h"

ANYTHING_NAMESPACE_BEGIN

// /data 和非 data 需要保持一致，最好有一种方式能够获取当前的状态
default_event_handler::default_event_handler(std::string persistent_index_dir, std::string volatile_index_dir)
    : base_event_handler(std::move(persistent_index_dir), std::move(volatile_index_dir)) {
    // Index the default mount point
    auto home_dir = get_home_directory();
    if (!home_dir.empty()) {
        add_index_delay(home_dir);
        insert_index_directory(home_dir);
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

    // Skip if path is not under home directory
    std::string home = get_home_directory();
    bool isUnderHome = event.src.rfind(home) == 0;
    if (!isUnderHome && !event.dst.empty()) {
        isUnderHome = event.dst.rfind(home) == 0;
    }
    if (!isUnderHome) {
        return;
    }

    if (event.act != ACT_RENAME_FILE &&
        event.act != ACT_RENAME_FOLDER &&
        Config::instance().isPathInBlacklist(event.src)) {
        return;
    }

    bool ignored = false;
    ignored = ignored_event(event.dst.empty() ? event.src : event.dst, ignored);
    if (!ignored) {
        if (event.act == ACT_NEW_FILE || event.act == ACT_NEW_SYMLINK ||
            event.act == ACT_NEW_LINK || event.act == ACT_NEW_FOLDER) {
            // Do not check for the existence of files; we trust the kernel module.
            add_index_delay(std::move(event.src));
        } else if (event.act == ACT_DEL_FILE || event.act == ACT_DEL_FOLDER) {
            remove_index_delay(std::move(event.src));
        } else if (event.act == ACT_RENAME_FILE) {
            bool is_src_blacklisted = Config::instance().isPathInBlacklist(event.src);
            bool is_dst_blacklisted = Config::instance().isPathInBlacklist(event.dst);

            if (is_src_blacklisted && is_dst_blacklisted) {
                return;
            } else if (is_src_blacklisted) {
                add_index_delay(std::move(event.dst));
            } else if (is_dst_blacklisted) {
                remove_index_delay(std::move(event.src));
            } else {
                update_index_delay(std::move(event.src), std::move(event.dst));
            }
        } else if (event.act == ACT_RENAME_FOLDER) {
            // Rename all files/folders in this folder(including this folder)
            bool is_src_blacklisted = Config::instance().isPathInBlacklist(event.src);
            bool is_dst_blacklisted = Config::instance().isPathInBlacklist(event.dst);

            if (is_src_blacklisted && is_dst_blacklisted) {
                return;
            }

            if (is_src_blacklisted) {
                add_index_delay(event.dst);
                scan_index_delay(std::move(event.dst));
                return;
            }

            QString oldPath = QString::fromStdString(event.src);
            for (auto const& result : traverse_directory(oldPath)) {
                std::string src = result.toStdString();
                std::string dst = src;
                dst.replace(0, event.src.length(), event.dst);
                if (is_dst_blacklisted) {
                    remove_index_delay(std::move(src));
                } else {
                    update_index_delay(std::move(src), std::move(dst));
                }
            }
        }
    }
}

ANYTHING_NAMESPACE_END
