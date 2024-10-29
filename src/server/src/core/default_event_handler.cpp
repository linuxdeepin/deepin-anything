#include "anything/core/default_event_handler.h"

#include "anything/utils/log.h"
#include "anything/utils/print_helper.h"
#include "anything/utils/string_helper.h"
#include "vfs_change_consts.h"


ANYTHING_NAMESPACE_BEGIN

default_event_handler::default_event_handler()
    : index_manager_("/home/dxnu/log-files/index-data-test-dir") {
    mnt_manager_.update();

    // 索引未建立，则扫描建立
    if (!index_manager_.indexed()) {
        int directories = 0;
        int files = 0;
        auto start = std::chrono::high_resolution_clock::now();

        for (const auto& mp : mnt_manager_.get_mount_points()) {
            // if (mp.target == "/" || mp.root != "/")
            //     continue;

            // std::cout << "Iterate: " << mp.target << "\n";
            // 不确定扫描哪些挂载点，先只扫描这三个
            if (mp.target == "/data"/* || mp.target == "/recovery" || mp.target == "/data"*/) {
                log::info("Scanning {}...", mp.target);
                scanner_.scan("/data/home/dxnu/Downloads", [this, &directories, &files](file_record record) {
                    // 过滤掉无法解析的文件路径，":" 是非法字符
                    if (contains(record.full_path, ":") || contains(record.full_path, "[") || contains(record.full_path, "]") ||
                        contains(record.full_path, "{") || contains(record.full_path, "}"))
                        return;

                    record.is_directory ? directories++ : files++;
                    index_manager_.add_index(std::move(record));
                    print(record);
                    // std::cout << "file_name: " << record.file_name << " full_path: " << record.full_path << " is_directory: " << record.is_directory
                    //     << " modified: " << record.modified << "\n";
                });
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> duration = end - start;
        log::info("Scan time: {} seconds", duration.count());
        log::info("Files: {}, Directories: {}", files, directories);
        index_manager_.commit();
    }

    log::info("Document size: {}", index_manager_.document_size());
    auto results = index_manager_.search_index(".XLSX");
    log::info("Found {} result(s).", results.size());
    for (const auto& record : results) {
        print(record);
    }

    index_manager_.test(L"/data/home/dxnu/Downloads/2024届地区信息.XLSX"); // dxnu md   md
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

        if (event.act == ACT_NEW_FILE || event.act == ACT_NEW_SYMLINK ||
            event.act == ACT_NEW_LINK || event.act == ACT_NEW_FOLDER) {
            auto record = file_helper::generate_file_record(std::move(event.src));
            if (record)
                index_manager_.add_index_delay(std::move(*record));
            // std::cout << "Insert: ";
            // print(record);
        } else if (event.act == ACT_DEL_FILE || event.act == ACT_DEL_FOLDER) {
            // std::cout << "Remove: " << event.src << "\n";
            index_manager_.remove_index(event.src);
        } else if (event.act == ACT_RENAME_FILE || event.act == ACT_RENAME_FOLDER) {
            std::cout << "Rename: " << event.src << " --> ";
            auto record = file_helper::generate_file_record(std::move(event.dst));
            if (record)
                print(*record);
        }
    }

}

void default_event_handler::process_documents_if_ready() {
    index_manager_.process_documents_if_ready();
}

bool default_event_handler::ignored_event(const std::string& path, bool ignored) {
    if (ends_with(path, ".longname"))
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

ANYTHING_NAMESPACE_END