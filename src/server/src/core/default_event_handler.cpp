#include "anything/core/default_event_handler.h"

#include "anything/utils/log.h"
#include "anything/utils/print_helper.h"
#include "anything/utils/string_helper.h"
#include "vfs_change_consts.h"

ANYTHING_NAMESPACE_BEGIN

default_event_handler::default_event_handler()
    : base_event_handler("/home/dxnu/log-files/index-data-test-dir"),
      records_(scanner_.parallel_scan("/home/dxnu")) {

    mnt_manager_.update();
    // 索引未建立，则扫描建立
    // if (!index_manager_.indexed()) {
    //     int directories = 0;
    //     int files = 0;
    //     auto start = std::chrono::high_resolution_clock::now();

    //     for (const auto& mp : mnt_manager_.get_mount_points()) {
    //         // if (mp.target == "/" || mp.root != "/")
    //         //     continue;

    //         // std::cout << "Iterate: " << mp.target << "\n";
    //         // 不确定扫描哪些挂载点，先只扫描这三个
    //         // 使用分批处理或多线程来降低 CPU Usage
    //         if (mp.target == "/data"/* || mp.target == "/recovery" || mp.target == "/data"*/) {
    //             log::info("Scanning {}...", mp.target);
    //             scanner_.scan("/data/home/dxnu/Downloads", [this, &directories, &files](file_record record) {
    //                 // Skip if the path contains any invalid character
    //                 if (contains_invalid_chars(record.full_path)) {
    //                     return;
    //                 }

    //                 record.is_directory ? directories++ : files++;
    //                 index_manager_.add_index_delay(std::move(record));
    //                 // print(record);
    //             });
    //         }
    //     }

    //     auto end = std::chrono::high_resolution_clock::now();
    //     std::chrono::duration<double> duration = end - start;
    //     log::info("Scan time: {} seconds", duration.count());
    //     log::info("Files: {}, Directories: {}", files, directories);
    //     index_manager_.commit();
    // }

    log::debug("Record size: {}", records_.size());
    // log::info("Document size: {}", index_manager_.document_size());
    // auto results = index_manager_.search_index("bench");
    // log::info("Found {} result(s).", results.size());
    // for (const auto& record : results) {
    //     print(record);
    // }

    // index_manager_.test(L"/data/home/dxnu/Downloads/2024届地区信息.XLSX"); // dxnu md   md
}

void default_event_handler::handle(fs_event event) {
    // const char* act_names[] = {"file_created", "link_created", "symlink_created", "dir_created", "file_deleted", "dir_deleted", "file_renamed", "dir_renamed"};

    // Update partition event
    if (event.act == ACT_MOUNT || event.act == ACT_UNMOUNT) {
        mnt_manager_.update();
        return;
    }

    std::string root;
    if (event.act < ACT_MOUNT) {
        unsigned int device = MKDEV(event.major, event.minor);
        if (!mnt_manager_.contains_device(device)) {
            log::warning("Unknown device, {}, dev: {}:{}, path: {}, cookie: {}",
                +event.act, event.major, event.minor, event.src, event.cookie);
            return;
        }

        root = mnt_manager_.get_mount_point(device);
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
        if (contains(event.src, "/data/home/dxnu/log-files/index-data-test-dir/"))
            return;

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

// `records_` is safe because it is accessed exclusively within this function,
// which is called by only one thread after `records_` is initialized.
// `index_manager_` is not safe.
// 此种是否要重新判断文件的存在情况，扫描记录后，未创建索引时，
// 用户将文件重命名或删除，此时记录便非最新，也就没有必要建立索引了
void default_event_handler::run_scheduled_task() {
    if (!records_.empty()) {
        size_t batch_size = std::min(size_t(500), records_.size());
        for (size_t i = 0; i < batch_size; ++i) {
            index_manager_.add_index_delay(std::move(records_.front()));
            records_.pop_front();
        }
    }
}

ANYTHING_NAMESPACE_END