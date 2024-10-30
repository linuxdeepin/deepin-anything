#include "anything/core/base_event_handler.h"
#include "anything/utils/log.h"
#include "anything/utils/string_helper.h"
#include <anythingadaptor.h>

base_event_handler::base_event_handler(std::string index_dir, QObject* parent)
    : QObject(parent), index_manager_(std::move(index_dir)) {
    new IAnythingAdaptor(this);
    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.registerObject("/com/deepin/OAnything", this);
    dbus.registerService("com.deepin.SAnything");
}

void base_event_handler::testMethod() {
}

base_event_handler::~base_event_handler() {}

void base_event_handler::process_documents_if_ready() {
    index_manager_.process_documents_if_ready();
}

bool base_event_handler::ignored_event(const std::string &path, bool ignored)
{
    if (anything::ends_with(path, ".longname"))
        return true; // 长文件名记录文件，直接忽略
    
    // 没有标记忽略前一条，则检查是否长文件目录
    if (!ignored) {
        // 向上找到一个当前文件的挂载点且匹配文件系统类型
        if (mnt_manager_.path_match_type(path, "fuse.dlnfs")) {
            // 长文件目录，标记此条事件被忽略
            return true;
        }
    }

    return false;
}
