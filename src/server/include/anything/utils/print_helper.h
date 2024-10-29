#ifndef ANYTHING_PRINT_HELPER_H_
#define ANYTHING_PRINT_HELPER_H_

#include <iostream>

#include "anything/common/anything_fwd.hpp"
#include "anything/common/file_record.hpp"

ANYTHING_NAMESPACE_BEGIN

inline void print(const file_record& record) {
    std::cout << "file_name: " << record.file_name << " full_path: " << record.full_path
              << " is_directory: " << record.is_directory << " modified: " << record.modified << "\n";
}

ANYTHING_NAMESPACE_END

#endif // ANYTHING_PRINT_HELPER_H_