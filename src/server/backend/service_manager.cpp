#include "service_manager.h"
#include "anything_adaptor.h"
#include <QDBusConnection>
#include <iostream>

namespace anything {

bool service_manager::register_service(std::string path) {
    QString qpath = QString::fromStdString(std::move(path));
    
    QDBusConnection connection = QDBusConnection::systemBus();
    if (!connection.interface()->isServiceRegistered(qpath)) {
        bool reg_result = connection.registerService(qpath);
        if (!reg_result) {
            std::cerr << "Cannot register the \"com.deepin.anything\" service.\n";
            return false;
        }

        std::cout << "Create AnythingAdaptor\n";
        Q_UNUSED(new AnythingAdaptor(&LFTManager::instance()));
        if (!connection.registerObject("/com/deepin/anything", &LFTManager::instance())) {
            std::cerr << "Cannot register to the D-Bus object: \"/com/deepin/anything\"";
            return false;
        }
    }

    std::cout << "deepin-anything-backend is running\n";
    return true;
}

} // namespace anything
