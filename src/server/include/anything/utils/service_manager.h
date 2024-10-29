#ifndef ANYTHING_SERVICE_MANAGER_H_
#define ANYTHING_SERVICE_MANAGER_H_

#include <string>

#include "anything/common/anything_fwd.hpp"


ANYTHING_NAMESPACE_BEGIN

struct service_manager {
    bool register_service(std::string path);
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_SERVICE_MANAGER_H_
