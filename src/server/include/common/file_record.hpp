#ifndef ANYTHING_FILE_RECORD_H_
#define ANYTHING_FILE_RECORD_H_

#include <filesystem>
#include <optional>
#include <string>

#include "common/anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

struct file_record {
    std::string file_name;
    std::string full_path;
};

namespace file_helper {

namespace fs = std::filesystem;

inline file_record make_file_record(const fs::path& p) {
    return file_record {
        .file_name    = p.filename().string(),
        .full_path    = p.string()
    };
}

} // namespace file_helper

ANYTHING_NAMESPACE_END

#endif // ANYTHING_FILE_RECORD_H_