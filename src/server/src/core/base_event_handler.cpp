#include "core/base_event_handler.h"

#include "common/file_record.hpp"
#include "utils/log.h"
#include "utils/string_helper.h"
#include <anythingadaptor.h>

base_event_handler::base_event_handler(std::string index_dir, QObject *parent)
    : QObject(parent), index_manager_(std::move(index_dir)) {
    new IAnythingAdaptor(this);
    QDBusConnection dbus = QDBusConnection::systemBus();
    QString service_name = "my.test.SAnything";
    QString object_name = "/my/test/OAnything";
    if (!dbus.interface()->isServiceRegistered(service_name)) {
        dbus.registerService(service_name);
        dbus.registerObject(object_name, this);
    }
}

base_event_handler::~base_event_handler() {}

void base_event_handler::process_documents_if_ready() {
    index_manager_.process_documents_if_ready();
}

// 没有线程安全问题，因为所有数据都处于同一线程
void base_event_handler::run_scheduled_task() {
    if (!records_.empty()) {
        size_t batch_size = std::min(size_t(500), records_.size());
        for (size_t i = 0; i < batch_size; ++i) {
            index_manager_.add_index_delay(std::move(records_.front()));
            records_.pop_front();
        }
    }
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

void base_event_handler::insert_pending_records(
    std::deque<anything::file_record> records) {
    records_.insert(records_.end(),
        std::make_move_iterator(records.begin()),
        std::make_move_iterator(records.end()));
}

std::size_t base_event_handler::record_size() const {
    return records_.size();
}

void base_event_handler::refresh_mount_status() {
    mnt_manager_.update();
}

bool base_event_handler::device_available(unsigned int device_id) const {
    return mnt_manager_.contains_device(device_id);
}

std::string base_event_handler::fetch_mount_point_for_device(unsigned int device_id) const {
    return mnt_manager_.get_mount_point(device_id);
}

std::string base_event_handler::get_index_directory() const {
    return index_manager_.index_directory();
}

void base_event_handler::set_index_change_filter(
    std::function<bool(const std::string&)> filter) {
    index_manager_.set_index_change_filter(std::move(filter));
}

// double base_event_handler::multiply(double factor0, double factor1)
// {
//     qDebug() << __PRETTY_FUNCTION__ << factor0 << factor1;
//     double product = factor0 * factor1;
//     emit newProduct(product);
//     return product;
// }

// double base_event_handler::divide(double dividend, double divisor)
// {
//     qDebug() << __PRETTY_FUNCTION__ << dividend << divisor;
//     double quotient = dividend / divisor;
//     emit newQuotient(quotient);
//     return quotient;
// }

QStringList base_event_handler::search(
    const QString& path, const QString& keywords,
    int offset, int max_count) {
    if (offset < 0)
        return {};

    return index_manager_.search(path, keywords, offset, max_count, true);
}

// 未特殊处理文件不存在的情况，只要最终不存在，就算成功
bool base_event_handler::removePath(const QString& fullPath) {
    auto path = fullPath.toStdString();
    index_manager_.remove_index(path);
    return !index_manager_.document_exists(path);
}

bool base_event_handler::hasLFT(const QString& path) {
    return index_manager_.document_exists(path.toStdString());
}

bool base_event_handler::addPath(const QString& fullPath) {
    auto path = fullPath.toStdString();
    auto record = anything::file_helper::generate_file_record(path);
    if (record) {
        index_manager_.add_index_delay(std::move(*record));
        return index_manager_.document_exists(path);
    }

    return false;
}

void base_event_handler::index_files_in_directory(const QString& directory_path) {
    (void)directory_path;
    // index_manager_.
}