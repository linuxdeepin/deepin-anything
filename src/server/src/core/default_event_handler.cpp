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
default_event_handler::default_event_handler(std::string persistent_index_dir, std::string volatile_index_dir)
    : base_event_handler(std::move(persistent_index_dir), std::move(volatile_index_dir)) {
    // Index the default mount point
    auto home_dir = std::string(g_get_home_dir());
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

const std::string& get_home_dir_event_path() {
    static const std::string home_dir_event_path = [] {
        char *home_dir = get_full_path(g_get_home_dir());
        if (home_dir == nullptr) {
            spdlog::error("Failed to get home directory");
            return std::string(g_get_home_dir());
        }
        auto home_dir_event_path = std::string(home_dir);
        g_free(home_dir);
        spdlog::debug("home_dir_event_path: {}", home_dir_event_path);
        return home_dir_event_path;
    }();
    return home_dir_event_path;
}

bool is_under_home(const std::string& path) {
    return path.rfind(get_home_dir_event_path()) == 0;
}

// 将路径中的 /persistent/home/user 替换为 /home/user
void replace_home_prefix(std::string& path) {
    static bool do_replace = [] {
        return get_home_dir_event_path() != g_get_home_dir();
    }();

    if (do_replace) {
        path.replace(0, get_home_dir_event_path().length(), g_get_home_dir());
    }
}

bool is_event_path_in_blacklist(const std::string& path) {
    static const std::vector<std::string> event_path_blocked_list = [] {
        auto path_blocked_list = Config::instance().get_path_blocked_list();
        auto home_dir_length = strlen(g_get_home_dir());
        for (auto& path : path_blocked_list) {
            if (path.rfind(g_get_home_dir()) == 0) {
                path.replace(0, home_dir_length, get_home_dir_event_path());
            }
        }
        return path_blocked_list;
    }();

    for (auto& blocked_path : event_path_blocked_list) {
        if (path.find(blocked_path) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool is_event_path_blocked(const std::string& path) {
    return !is_under_home(path) || is_event_path_in_blacklist(path);
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

    if (event.act != ACT_RENAME_FILE &&
        event.act != ACT_RENAME_FOLDER &&
        is_event_path_blocked(event.src)) {
        return;
    }

    bool ignored = false;
    ignored = ignored_event(event.dst.empty() ? event.src : event.dst, ignored);
    if (!ignored) {
        if (event.act == ACT_NEW_FILE || event.act == ACT_NEW_SYMLINK ||
            event.act == ACT_NEW_LINK || event.act == ACT_NEW_FOLDER) {
            // Do not check for the existence of files; we trust the kernel module.
            replace_home_prefix(event.src);
            add_index_delay(std::move(event.src));
        } else if (event.act == ACT_DEL_FILE || event.act == ACT_DEL_FOLDER) {
            replace_home_prefix(event.src);
            remove_index_delay(std::move(event.src));
        } else if (event.act == ACT_RENAME_FILE) {
            bool isSrcBlocked = is_event_path_blocked(event.src);
            bool isDstBlocked = is_event_path_blocked(event.dst);

            if (isSrcBlocked && isDstBlocked) {
                return;
            } else if (isSrcBlocked) {
                replace_home_prefix(event.dst);
                add_index_delay(std::move(event.dst));
            } else if (isDstBlocked) {
                replace_home_prefix(event.src);
                remove_index_delay(std::move(event.src));
            } else {
                replace_home_prefix(event.src);
                replace_home_prefix(event.dst);
                update_index_delay(std::move(event.src), std::move(event.dst));
            }
        } else if (event.act == ACT_RENAME_FOLDER) {
            // Rename all files/folders in this folder(including this folder)
            bool isSrcBlocked = is_event_path_blocked(event.src);
            bool isDstBlocked = is_event_path_blocked(event.dst);

            if (isSrcBlocked && isDstBlocked) {
                return;
            }

            if (isSrcBlocked) {
                replace_home_prefix(event.dst);
                add_index_delay(event.dst);
                scan_index_delay(std::move(event.dst));
                return;
            }

            replace_home_prefix(event.src);
            replace_home_prefix(event.dst);
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
