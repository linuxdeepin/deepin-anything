#include "anything/core/disk_scanner.h"

#include <string>

#include <mntent.h>

#include "anything/utils/log.h"

ANYTHING_NAMESPACE_BEGIN

disk_scanner::disk_scanner(const char* mounts)
    : fmounts_(setmntent(mounts, "r")) {
    if (fmounts_) {
        struct mntent *ent;
        while ((ent = getmntent(fmounts_)) != NULL) {
            std::string fsname(ent->mnt_fsname);
            std::string mount_point(ent->mnt_dir);
            std::string fs_type(ent->mnt_type);

            // Filter unnecessary file system types
            if (fs_type == "proc" || fs_type == "sysfs" || fs_type == "devtmpfs" || fs_type == "tmpfs" || fs_type == "devpts" || fs_type == "securityfs")
                continue;

            // Skip root and user-specific mounts like gvfs
            if (mount_point == "/" || mount_point.find("/run/user/") == 0)
                continue;
            
            mounted_paths_.push_back(mount_point);
        }
    }
}

disk_scanner::~disk_scanner() {
    if (fmounts_)
        endmntent(fmounts_);
}

std::vector<fs::path> disk_scanner::mounted_paths() const {
    return mounted_paths_;
}

std::size_t disk_scanner::mounts_size() const {
    return mounted_paths_.size();
}

std::deque<file_record> disk_scanner::parallel_scan(const fs::path& root) const {
    log::debug("Scanning {}...", root.string());
    std::deque<file_record> records;
    fs::recursive_directory_iterator dirpos{ root, fs::directory_options::skip_permission_denied };
    for (auto it = begin(dirpos); it != end(dirpos); ++it) {
        // Skip hidden files and folders
        if (is_hidden(it->path())) {
            // Prevent recursion into hidden folders
            if (fs::is_directory(it->path())) {
                it.disable_recursion_pending();
            }
            continue;
        }

        if (std::filesystem::exists(it->path())) {
            auto record = file_helper::generate_file_record(it->path());
            if (record) {
                records.push_back(std::move(*record));
            }
        }
    }

    return records;
}

bool disk_scanner::is_hidden(const fs::path& p) const {
    auto filename = p.filename().string();
    return filename == ".." || filename == "." || filename[0] == '.';
}
ANYTHING_NAMESPACE_END