#ifndef ANYTHING_MOUNT_MANAGER_H_
#define ANYTHING_MOUNT_MANAGER_H_

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "anything/common/anything_fwd.hpp"


ANYTHING_NAMESPACE_BEGIN

#define MKDEV(ma,mi)    ((ma)<<8 | (mi))


enum class mountinfo_field {
    mount_id = 0,          // 挂载点 ID
    parent_id = 1,         // 父级 ID
    major_minor = 2,       // 设备号（主:次）
    root = 3,              // 根路径
    mount_point = 4,       // 挂载路径
    mount_options = 5,     // 挂载选项 (跳过)
    optional_fields = 6,   // 挂载选项 (跳过)
    shared_id = 7,         // 共享 ID (跳过)
    separator = 8,         // 分隔符 (未解析)
    file_system_type = 9,  // 文件系统类型
    source_device = 10,    // 源设备 (跳过)
    fs_options = 11        // 挂载选项 (跳过)
};

struct mount_point {
    std::string source;
    std::string real_device;
    std::string target;
    std::string root;
    std::string type;
    dev_t device_id = 0;
};


class mount_manager {
public:
    explicit mount_manager();

    bool update();

    bool contains_device(unsigned int key) const;

    std::string get_mount_point(unsigned int device) const;

    const std::vector<mount_point>& get_mount_points();
    
    /////////////////////////////

    bool update_mount_points();

    std::string find_mount_point(const std::string& path, bool hardreal = false);

    bool path_match_type(const std::string& path, const std::string& type);

private:
    bool write_vfs_unnamed_device(const std::string& str);
    bool read_vfs_unnamed_device(std::unordered_set<std::string>& devices);
    bool update_vfs_unnamed_device(const std::unordered_set<std::string>& new_devices);

private:
    std::string mountinfo_path_;
    std::unordered_map<unsigned int, std::string> mounts_; // key: major:minor value: mount point
    std::vector<mount_point> mount_points_;
};


ANYTHING_NAMESPACE_END

#endif // ANYTHING_MOUNT_MANAGER_H_