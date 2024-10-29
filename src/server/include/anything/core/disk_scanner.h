#ifndef ANYTHING_DISK_SCANNER_H_
#define ANYTHING_DISK_SCANNER_H_

#include <filesystem>
#include <fstream>
#include <vector>

#include "anything/common/anything_fwd.hpp"
#include "anything/common/file_record.hpp"


ANYTHING_NAMESPACE_BEGIN


namespace fs = std::filesystem;

/**
 * @class disk_scanner
 * @brief A class to scan mounted disks and perform operations on their files and directories.
 * 
 * The `disk_scanner` class provides functionality to scan files and directories
 * from mounted disk paths and execute user-defined operations on them. 
 */
class disk_scanner
{
public:
    /**
     * @brief Constructor for disk_scanner.
     * 
     * Initializes the disk scanner by reading the mounted paths. 
     * The default mount file path is `/proc/mounts`.
     * 
     * @param mounts Path to the file containing mount points. Defaults to "/proc/mounts".
     */
    explicit disk_scanner(const char* mounts = "/proc/mounts");
    ~disk_scanner();

    /**
     * @brief Get the list of mounted paths.
     * 
     * @return A vector of filesystem paths representing the mounted directories.
     */
    std::vector<fs::path> mounted_paths() const;

    std::size_t mounts_size() const;

    /**
     * @brief Scans all mounted paths and applies a function to each directory entry.
     * 
     * This function iterates over all directories and files within the mounted paths
     * and applies the user-specified function to each directory entry.
     * 
     * @tparam F A callable type that accepts two arguments: a `directory_entry_type` 
     * and a constant reference to a `fs::directory_entry`.
     * @param func A callable object (function, lambda, etc.) that will be applied to each directory entry.
     * @throws std::runtime_error If an error occurs during scanning.
     */
    template<typename F,
        typename std::enable_if_t<std::is_invocable_v<F, file_record>>* = nullptr>
    void scan(F&& func) const {
        scan("/data/home/dxnu/dxnu-obsidian", std::forward<F>(func));
        // for (const auto& mount_point : mounted_paths_) {
        //     // std::cout << "Mount point: " << mount_point.string() << "\n";
        //     scan(mount_point, std::forward<F>(func));
        // }
    }
    
    /**
     * @brief Scans a specific directory tree and applies a function to each entry.
     * 
     * This function performs a recursive scan starting from the provided root path
     * and applies the given function to each file or directory entry.
     * 
     * @tparam F A callable type that accepts two arguments: a `directory_entry_type` 
     * and a constant reference to a `fs::directory_entry`.
     * @param root The root path where the scan will begin.
     * @param func A callable object (function, lambda, etc.) that will be applied to each directory entry.
     * @throws std::runtime_error If an error occurs while scanning a specific directory.
     */
    template<typename F,
        typename std::enable_if_t<std::is_invocable_v<F, file_record>>* = nullptr>
    void scan(const fs::path& root, F&& func) const {
        for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
            auto filename = it->path().filename().string();
            
            // 过滤隐藏文件和隐藏文件夹
            if (filename[0] == '.') {
                // 禁止递归进入隐藏文件夹
                if (fs::is_directory(it->path()))
                    it.disable_recursion_pending();
                continue;
            }

            if (std::filesystem::exists(it->path())) {
                auto record = file_helper::generate_file_record(it->path());
                if (record)
                    func(std::move(*record));
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

private:
    /// List of mounted filesystem paths.
    FILE* fmounts_;
    std::vector<fs::path> mounted_paths_;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_DISK_SCANNER_H_
