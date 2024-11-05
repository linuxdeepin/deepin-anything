#ifndef ANYTHING_FS_EVENT_H_
#define ANYTHING_FS_EVENT_H_

#include <string>

#include "common/anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

struct fs_event {
    uint8_t     act;
    uint32_t    cookie;
    uint16_t    major;
    uint8_t     minor;
    std::string src;
    std::string dst;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_FS_EVENT_H_