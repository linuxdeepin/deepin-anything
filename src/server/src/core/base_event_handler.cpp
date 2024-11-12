#include "core/base_event_handler.h"

#include "common/file_record.hpp"
#include "utils/log.h"
#include "utils/string_helper.h"
#include <anythingadaptor.h>

base_event_handler::base_event_handler(std::string index_dir, QObject *parent)
    : QObject(parent), index_manager_(std::move(index_dir)), batch_size_(100) {
    new IAnythingAdaptor(this);
    QDBusConnection dbus = QDBusConnection::systemBus();
    QString service_name = "my.test.SAnything";
    QString object_name = "/my/test/OAnything";
    if (!dbus.interface()->isServiceRegistered(service_name)) {
        dbus.registerService(service_name);
        dbus.registerObject(object_name, this);
    }
}

base_event_handler::~base_event_handler() {
    pool_.wait_for_tasks();
}

void base_event_handler::terminate_processing() {
    
}

void base_event_handler::run_scheduled_task() {
    // if (!records_.empty()) {
    //     size_t batch_size = std::min(size_t(500), records_.size());
    //     for (size_t i = 0; i < batch_size; ++i) {
    //         {
    //             std::lock_guard<std::mutex> lock(mtx_);
    //             index_manager_.add_index_delay(std::move(records_.front()));
    //         }
    //         records_.pop_front();
    //     }
    // }

    // cv_.notify_one();
}

void base_event_handler::set_batch_size(std::size_t size) {
    batch_size_ = size;
}

bool base_event_handler::ignored_event(const std::string& path, bool ignored)
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
    index_change_filter_ = std::move(filter);
}

void base_event_handler::add_index_delay(std::string path) {
    if (should_be_filtered(path)) {
        return;
    }
    // anything::log::debug("{} begin", __PRETTY_FUNCTION__);
    // index_manager_.add_index(std::move(path));
    addition_jobs_.push_back(std::move(path));
    if (addition_jobs_.size() >= batch_size_) {
        eat_jobs(addition_jobs_, batch_size_);
        // std::vector<std::string> processing_jobs;
        // processing_jobs.insert(
        //     processing_jobs.end(),
        //     std::make_move_iterator(addition_jobs_.begin()),
        //     std::make_move_iterator(addition_jobs_.begin() + batch_size_));
        // addition_jobs_.erase(addition_jobs_.begin(), addition_jobs_.begin() + batch_size_);
        // pool_.enqueue_detach([this, processing_jobs = std::move(processing_jobs)]() {
        //     for (auto&& job : processing_jobs) {
        //         index_manager_.add_index(std::move(job));
        //     }
        // });
    }
}

void base_event_handler::remove_index_delay(std::string term) {
    if (should_be_filtered(term)) {
        return;
    }
    anything::log::debug("Removed index: {}", term);
    index_manager_.remove_index(term);
    // std::lock_guard<std::mutex> lock(index_jobs_mtx_);
    // index_jobs_.emplace_back(std::move(term), anything::index_job_type::remove);
    // if (index_jobs_.size() >= batch_size_) {
    //     std::vector<anything::index_job> processing_jobs;
    //     processing_jobs.insert(
    //         processing_jobs.end(),
    //         std::make_move_iterator(index_jobs_.begin()),
    //         std::make_move_iterator(index_jobs_.begin() + batch_size_));
    //     index_jobs_.erase(index_jobs_.begin(), index_jobs_.begin() + batch_size_);
    //     pool_.enqueue_detach([this, processing_jobs = std::move(processing_jobs)]() {
    //         for (auto&& job : processing_jobs) {
    //             if (job.type == anything::index_job_type::add) {
    //                 anything::log::debug("Indexed {}", job.src);
    //                 index_manager_.add_index(std::move(job.src));
    //             } else if (job.type == anything::index_job_type::remove) {
    //                 anything::log::debug("Removed index: {}", job.src);
    //                 index_manager_.remove_index(job.src);
    //             }
    //         }
    //     });
    // }
}

bool base_event_handler::should_be_filtered(const anything::file_record& record) const {
    return should_be_filtered(record.full_path);
}

bool base_event_handler::should_be_filtered(const std::string& path) const {
    if (index_change_filter_) {
        return std::invoke(index_change_filter_, path);
    }

    return false;
}

QStringList base_event_handler::search(
    const QString& path, const QString& keywords,
    int offset, int max_count) {
    if (offset < 0) {
        return {};
    }

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

void base_event_handler::addPath(const QString& fullPath) {
    auto path = fullPath.toStdString();
    if (std::filesystem::exists(path)) {
        add_index_delay(path);
    }
}

void base_event_handler::index_files_in_directory(const QString& directory_path) {
    (void)directory_path;
    // index_manager_.
}