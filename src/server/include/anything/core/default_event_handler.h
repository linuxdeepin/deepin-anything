#ifndef ANYTHING_EVENT_HANDLER_H_
#define ANYTHING_EVENT_HANDLER_H_

#include "anything/common/anything_fwd.hpp"
#include "anything/common/fs_event.h"
#include "anything/core/disk_scanner.h"
#include "anything/core/file_index_manager.h"
#include "anything/core/mount_manager.h"


ANYTHING_NAMESPACE_BEGIN

class default_event_handler {
public:
    explicit default_event_handler();
    void handle(fs_event event);

    void process_documents_if_ready();

private:
    bool ignored_event(const std::string& path, bool ignored);

private:
    mount_manager mnt_manager_;
    disk_scanner scanner_;
    file_index_manager index_manager_;
    std::unordered_map<uint32_t, std::string> rename_from_;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_EVENT_HANDLER_H_