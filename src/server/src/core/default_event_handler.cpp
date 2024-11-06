#include "core/default_event_handler.h"

#include "utils/log.h"
#include "utils/print_helper.h"
#include "utils/string_helper.h"
#include "vfs_change_consts.h"

ANYTHING_NAMESPACE_BEGIN

// /data 和非 data 需要保持一致，最好有一种方式能够获取当前的状态
default_event_handler::default_event_handler(std::string index_dir)
    : base_event_handler(std::move(index_dir)) {
    // Index the default mount point
    insert_pending_records(scanner_.parallel_scan("/data/home/dxnu"));

    // Initialize mount cache
    refresh_mount_status();

    // index_directory 设置时必须要是完整路径
    set_index_change_filter([this](const std::string& path) {
        auto index_directory = get_index_directory();
        auto names = split(path, "/");
        return starts_with(path, index_directory) ||
               (starts_with(index_directory, "/") && starts_with(path, "/data" + index_directory)) ||
               std::any_of(names.begin(), names.end(), [](const std::string& name) { return starts_with(name, "."); });
    });

    log::debug("Record size: {}", record_size());
    log::debug("Document size: {}", index_manager_.document_size());
    auto results = index_manager_.search_index("test haha");
    log::debug("Found {} result(s).", results.size());
    for (const auto& record : results) {
        print(record);
    }

    // index_manager_.test(L"/data/home/dxnu/Downloads/2024届地区信息.XLSX"); // dxnu md   md
}

void default_event_handler::handle(fs_event event) {
    // const char* act_names[] = {"file_created", "link_created", "symlink_created", "dir_created", "file_deleted", "dir_deleted", "file_renamed", "dir_renamed"};

    // Update partition event
    if (event.act == ACT_MOUNT || event.act == ACT_UNMOUNT) {
        log::info(event.act == ACT_MOUNT ? "Mount a device: {}" : "Unmount a device: {}", event.src);
        refresh_mount_status();
        return;
    }

    std::string root;
    if (event.act < ACT_MOUNT) {
        unsigned int device_id = MKDEV(event.major, event.minor);
        if (!device_available(device_id)) {
            log::warning("Unknown device, {}, dev: {}:{}, path: {}, cookie: {}",
                +event.act, event.major, event.minor, event.src, event.cookie);
            return;
        }

        root = fetch_mount_point_for_device(device_id);
        // std::cout << "root: " << root << "\n";
        if (root == "/")
            root.clear();
    }

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
        log::warning("Don't support file action: {}", +event.act);
        return;
    default:
        log::warning("Unknown file action: {}", +event.act);
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

    // 事件已完全解析，开始处理
    // std::cout << "message handling: [act:\"" << act_names[event.act] << "\", src:\"" << event.src
    //         << "\", dst:\"" << event.dst << "\"]\n";
    
    // Preparations are done, starting to process the event.
    bool ignored = false;
    ignored = ignored_event(event.dst.empty() ? event.src : event.dst, ignored);
    if (!ignored) {
        // log::info("Received a {} message(src={},dst={})", act_names[event.act], event.src, event.dst);

        if (event.act == ACT_NEW_FILE || event.act == ACT_NEW_SYMLINK ||
            event.act == ACT_NEW_LINK || event.act == ACT_NEW_FOLDER) {
            auto record = file_helper::generate_file_record(std::move(event.src));
            if (record) {
                index_manager_.add_index_delay(std::move(*record));
            }
        } else if (event.act == ACT_DEL_FILE || event.act == ACT_DEL_FOLDER) {
            index_manager_.remove_index(event.src);
        } else if (event.act == ACT_RENAME_FILE || event.act == ACT_RENAME_FOLDER) {
            auto record = file_helper::generate_file_record(std::move(event.dst));
            if (record) {
                index_manager_.update_index(event.src, std::move(*record));
            }
        }
    }
}

ANYTHING_NAMESPACE_END