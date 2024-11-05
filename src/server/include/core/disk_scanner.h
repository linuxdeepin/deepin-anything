#ifndef ANYTHING_DISK_SCANNER_H_
#define ANYTHING_DISK_SCANNER_H_

// #include <atomic>
// #include <execution>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>

#include "common/anything_fwd.hpp"
#include "common/file_record.hpp"

ANYTHING_NAMESPACE_BEGIN

namespace fs = std::filesystem;

/// A class to scan mounted disks and perform operations on their files and directories.
class disk_scanner
{
public:
    /// Initializes the disk scanner by reading the mounted paths. 
    explicit disk_scanner(const char* mounts = "/proc/mounts");
    ~disk_scanner();

    /// Return the list of mounted paths.
    std::vector<fs::path> mounted_paths() const;

    std::size_t mounts_size() const;

    /// Scans all mounted paths and applies a function to each directory entry.
    template<typename F, typename std::enable_if_t<std::is_invocable_v<F, file_record>>* = nullptr>
    void scan(F&& func) const {
        scan("/data/home/dxnu/dxnu-obsidian", std::forward<F>(func));
        // for (const auto& mount_point : mounted_paths_) {
        //     // std::cout << "Mount point: " << mount_point.string() << "\n";
        //     scan(mount_point, std::forward<F>(func));
        // }
    }

    /// Recursively scans the given directory and applies a user-provided function on each valid file record.
    template<typename F, typename std::enable_if_t<std::is_invocable_v<F, file_record>>* = nullptr>
    void scan(const fs::path& root, F&& func) const {
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied);
             it != fs::recursive_directory_iterator();
             ++it) {
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
                    func(std::move(*record));
                }
            }
        }
        // 不过滤任务文件用这种写法，更简单
        // for (const auto& entry : fs::recursive_directory_iterator(root)) {
        //     if (std::filesystem::exists(entry.path())) {
        //         auto last_write_time = fs::last_write_time(entry.path());
        //         auto duration = last_write_time.time_since_epoch(); // 获取自纪元以来的持续时间
        //         auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
                
        //         file_record record;
        //         // record.root_path = entry.path().parent_path();
        //         // record.file_name = entry.path().filename();
        //         record.full_path = entry.path().string();
        //         record.modified = milliseconds; // 转换为毫秒

        //         if (fs::is_directory(entry.path())) {
        //             record.is_directory = true;
        //             func(std::move(record));
        //         }
        //         else if (fs::is_regular_file(entry.path())) {
        //             record.is_directory = false;
        //             func(std::move(record));
        //         }
        //     }
        // }
    }

    std::deque<file_record> parallel_scan(const fs::path& root) const;

private:
    bool is_hidden(const fs::path& p) const;

private:
    /// List of mounted filesystem paths.
    FILE* fmounts_;
    std::vector<fs::path> mounted_paths_;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_DISK_SCANNER_H_