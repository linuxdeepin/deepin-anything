#ifndef ANYTHING_SERVICE_MANAGER_H
#define ANYTHING_SERVICE_MANAGER_H

#include <string>


namespace anything {

struct service_manager {
    bool register_service(std::string path);
};

} // namespace anything

#endif // ANYTHING_SERVICE_MANAGER_H
