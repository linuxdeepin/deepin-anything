#ifndef ANYTHING_EVENT_HANDLER_H_
#define ANYTHING_EVENT_HANDLER_H_

#include "anything/core/base_event_handler.h"

ANYTHING_NAMESPACE_BEGIN

class default_event_handler : public base_event_handler {
public:
    explicit default_event_handler();
    
    void handle(fs_event event) override;

private:
    std::unordered_map<uint32_t, std::string> rename_from_;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_EVENT_HANDLER_H_