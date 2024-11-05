#include "core/mount_manager.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <unordered_set>

#include <libmount.h>
#include <sys/sysmacros.h> // major(), minor()

#include "utils/enum_helper.h"
#include "utils/string_helper.h"

ANYTHING_NAMESPACE_BEGIN

mount_manager::mount_manager()
    : mountinfo_path_{"/proc/self/mountinfo"} {
    update_mount_points();
}

bool mount_manager::update() {
    std::ifstream file_mountinfo(mountinfo_path_);
    if (!file_mountinfo.is_open()) {
        std::cerr << "open file failed: " << mountinfo_path_ << "\n";
        return false;
    }

    std::string line;
    std::unordered_set<std::string> dlnfs_devs;
    while (std::getline(file_mountinfo, line)) {
        auto mountinfo = split(line, " ");
        auto major_minor = split(mountinfo[to_underlying(mountinfo_field::major_minor)], ":");
        unsigned int major = std::stoul(major_minor[0]);
        unsigned int minor = std::stoul(major_minor[1]);
        if (major == 0 && mountinfo[to_underlying(mountinfo_field::file_system_type)] != "fuse.dlnfs")
            continue;

        if (mountinfo[to_underlying(mountinfo_field::root)] == "/") {
            std::cout << mountinfo[to_underlying(mountinfo_field::mount_point)] << "\n";
            mounts_.emplace(MKDEV(major, minor), mountinfo[to_underlying(mountinfo_field::mount_point)]);
            if (major == 0 && mountinfo[to_underlying(mountinfo_field::file_system_type)] == "fuse.dlnfs")
                dlnfs_devs.insert(major_minor[1]);
        }
    }

    std::cout << "-------------------------------\n";
    return update_vfs_unnamed_device(dlnfs_devs);
}

bool mount_manager::contains_device(unsigned int key) const {
    return mounts_.count(key);
}

std::string mount_manager::get_mount_point(unsigned int device) const {
    return mounts_.at(device);
}

const std::vector<mount_point>& mount_manager::get_mount_points() {
    std::cout << "mount_points: " << mount_points_.size() << " mounts: " << mounts_.size() << "\n";
    return mount_points_;
}

bool mount_manager::update_mount_points() {
    mnt_init_debug(0);

    using unique_table_t = std::unique_ptr<libmnt_table, decltype(&mnt_free_table)>; 
    unique_table_t tb(mnt_new_table(), &mnt_free_table);
    if (!tb)
        return false;
    
    mnt_table_set_parser_errcb(tb.get(),
        [](libmnt_table *tb, const char *filename, int line) {
            (void)tb;
            std::cerr << filename << ": parse error at line " << line << " -- ignored\n";
            return 1;
        });
    
    // 使用 "/proc/self/mountinfo"，否则导致NTFS挂载点被隐藏
    if (mnt_table_parse_mtab(tb.get(), "/proc/self/mountinfo") != 0) {
        std::cerr << "Can't read /proc/self/mountinfo\n";
        return false;
    }

    // 解析成功，清除之前的 mount 信息
    mount_points_.clear();

    // 向前查找，保存信息与 "cat /proc/self/mountinfo" 得到信息一致
    libmnt_iter* iter = mnt_new_iter(MNT_ITER_FORWARD);
    libmnt_fs* fs;

    while (mnt_table_next_fs(tb.get(), iter, &fs) == 0) {
        mount_point info;
        info.device_id = mnt_fs_get_devno(fs);
        info.type = mnt_fs_get_fstype(fs);
        if (!major(info.device_id) && info.type != "fuse.dlnfs") {
            // std::cout << "ignore the virtual: " << info.type << "\n";
            continue;
        }
        info.source = mnt_fs_get_source(fs);
        info.target = mnt_fs_get_target(fs);
        info.root = mnt_fs_get_root(fs);
        info.real_device = info.source;

        std::cout << "device_id: " << info.device_id << ", type: " << info.type
            << ", source: " << info.source << ", target: " << info.target
            << ", root: " << info.root << ", real_device: " << info.real_device << "\n";
        mount_points_.push_back(std::move(info));
    }

    mnt_free_iter(iter);

    std::cout << "-------------------------------\n";
    return true;
}

std::string mount_manager::find_mount_point(const std::string& path, bool hardreal) {
    std::string result;
    std::string result_path = path;

    for (;;) {
        char* mp = mnt_get_mountpoint(result_path.c_str());
        if (mp) {
            result = std::string(mp);
            if (hardreal) {
                bool find_virtual = false;
                for (const auto& info : mount_points_) {
                    // 找到挂载点相同，但是虚拟设备（major=0），向上一级找到真实设备挂载点
                    if (result == info.target && !major(info.device_id)) {
                        // 赋值当前挂载点，进入向上一级目录
                        result_path = result;
                        find_virtual = true;
                        break;
                    }
                }
                if (!find_virtual)
                    break; // 遍历完但是没有找到虚拟设备，返回当前挂载点
            } else {
                break; // 不需要向上找到真实设备挂载，直接返回
            }
        }

        // 已经向上找到根'/', 返回
        if (result_path == "/") {
            result = result_path;
            break;
        }

        auto last_dir_split_pos = result_path.find_last_of('/');
        if (last_dir_split_pos == std::string::npos)
            break;
        
        result_path = result_path.substr(0, last_dir_split_pos);
        if (result_path.empty())
            result_path = "/";
    }

    return result;
}

bool mount_manager::path_match_type(const std::string& path, const std::string& type) {
    auto point = find_mount_point(path);
    for (const auto& info : mount_points_) {
        if (point == info.target && type == info.type)
            return true;
    }

    return false;
}

bool mount_manager::write_vfs_unnamed_device(const std::string &str) {
    std::ofstream file("/sys/kernel/vfs_monitor/vfs_unnamed_devices");
    if (!file.is_open()) {
        std::cerr << "Failed to open file: /sys/kernel/vfs_monitor/vfs_unnamed_devices\n"; 
        return false;
    }

    file << str;
    return true;
}

bool mount_manager::read_vfs_unnamed_device(std::unordered_set<std::string> &devices) {
    std::ifstream file("/sys/kernel/vfs_monitor/vfs_unnamed_devices");
    if (!file.is_open()) {
        std::cerr << "Failed to open file: /sys/kernel/vfs_monitor/vfs_unnamed_devices\n"; 
        return false;
    }

    std::string line;
    if (std::getline(file, line)) {
        // Only includes a whitespace in this line
        if (line.find_first_not_of(' ') == std::string::npos)
            return false;

        std::cout << "line(read_vfs_unnamed_device): " << line << "\n";
        auto minors = split(line, ",");
        for (auto&& minor : minors)
            devices.insert(std::move(minor));
    }

    std::cout << "devices: " << devices.size() << " 1: " << *devices.begin() << "\n";
    return true;
}

bool mount_manager::update_vfs_unnamed_device(const std::unordered_set<std::string>& new_devices) {
    std::unordered_set<std::string> old_devices;
    if (!read_vfs_unnamed_device(old_devices))
        return false;

    // Remove the old devices
    std::vector<std::string> removed_devices;
    std::copy_if(old_devices.begin(), old_devices.end(), std::back_inserter(removed_devices),
        [&new_devices](const std::string& device) { return new_devices.count(device) == 0; });
    for (const auto& minor : removed_devices) {
        if (!write_vfs_unnamed_device("r" + minor))
            return false;
    }

    // Add the new devices
    std::vector<std::string> added_devices;
    std::copy_if(new_devices.begin(), new_devices.end(), std::back_inserter(added_devices),
        [&old_devices](const std::string& device) { return old_devices.count(device) == 0; });
    for (const auto& minor : added_devices) {
        if (!write_vfs_unnamed_device("a" + minor))
            return false;
    }

    return true;
}

ANYTHING_NAMESPACE_END