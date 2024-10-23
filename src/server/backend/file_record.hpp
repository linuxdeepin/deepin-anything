#ifndef ANYTHING_FILE_RECORD_H_
#define ANYTHING_FILE_RECORD_H_

#include <filesystem>
#include <optional>
#include <string>

#include "anything_fwd.hpp"


ANYTHING_NAMESPACE_BEGIN

struct file_record {
    std::string file_name;
    std::string full_path;
	bool is_directory;
    int64_t modified; // milliseconds time since epoch
};


namespace file_helper {

namespace fs = std::filesystem;

inline std::optional<file_record> generate_file_record(const fs::path& p) {
    if (!fs::exists(p))
        return std::nullopt;

    auto file_name = p.filename().string();
    auto is_directory = fs::is_directory(p);
    auto last_write_time = fs::last_write_time(p);
    auto duration = last_write_time.time_since_epoch();
    auto milliseconds =  std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    return file_record {
        .file_name=std::move(file_name),
        .full_path=p.string(),
        .is_directory=is_directory,
        .modified=milliseconds
    };
}

} // namespace file_helper

ANYTHING_NAMESPACE_END

#endif // ANYTHING_FILE_RECORD_H_