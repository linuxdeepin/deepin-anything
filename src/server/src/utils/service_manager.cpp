#include "anything/utils/service_manager.h"

#include <QDBusConnection>

#include "anything/utils/log.h"

ANYTHING_NAMESPACE_BEGIN

bool service_manager::register_service(std::string path) {
    (void)path;
    // QString qpath = QString::fromStdString(std::move(path));
    
    // QDBusConnection connection = QDBusConnection::systemBus();
    // if (!connection.interface()->isServiceRegistered(qpath)) {
    //     bool reg_result = connection.registerService(qpath);
    //     if (!reg_result) {
    //         log::error("Cannot register the \"com.deepin.anything\" service.");
    //         return false;
    //     }

    //     log::debug("Create AnythingAdaptor");
    //     Q_UNUSED(new AnythingAdaptor(&LFTManager::instance()));
    //     if (!connection.registerObject("/com/deepin/anything", &LFTManager::instance())) {
    //         log::error("Cannot register to the D-Bus object: \"/com/deepin/anything\"");
    //         return false;
    //     }
    // }
    return true;
}

ANYTHING_NAMESPACE_END