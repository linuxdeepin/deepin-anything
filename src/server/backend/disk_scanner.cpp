#include "disk_scanner.h"

#include <string>

#include <mntent.h>


ANYTHING_NAMESPACE_BEGIN


disk_scanner::disk_scanner(const char* mounts)
    : fmounts_(setmntent(mounts, "r"))
{
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

disk_scanner::~disk_scanner()
{
    if (fmounts_)
        endmntent(fmounts_);
}

std::vector<fs::path> disk_scanner::mounted_paths() const
{
    return mounted_paths_;
}

std::size_t disk_scanner::mounts_size() const
{
    return mounted_paths_.size();
}

ANYTHING_NAMESPACE_END