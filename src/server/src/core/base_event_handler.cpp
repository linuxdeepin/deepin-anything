#include "core/base_event_handler.h"

#include "common/file_record.hpp"
#include "utils/log.h"
#include "utils/string_helper.h"
#include <anythingadaptor.h>

base_event_handler::base_event_handler(std::string index_dir, QObject *parent)
    : QObject(parent), index_manager_(std::move(index_dir)), batch_size_(100),
      worker_(std::thread(&base_event_handler::worker_loop, this)) {
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

void base_event_handler::terminate_processing() {
    {
        std::lock_guard<std::mutex> lock(index_jobs_mtx_);
        should_stop_ = true;
    }

    cv_.notify_one();

    if (worker_.joinable()) {
        auto thread_id = worker_.get_id();
        worker_.join();
        anything::log::info("Worker thread {} has exited.", thread_id);
    }
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
    std::lock_guard<std::mutex> lock(index_jobs_mtx_);
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
    std::lock_guard<std::mutex> lock(index_jobs_mtx_);
    index_change_filter_ = std::move(filter);
}

void base_event_handler::add_index_delay(std::string path) {
    std::lock_guard<std::mutex> lock(index_jobs_mtx_);
    index_jobs_.emplace(std::move(path), anything::index_job_type::add);
    if (index_jobs_.size() > batch_size_) {
        cv_.notify_one();
    }
}

void base_event_handler::remove_index_delay(std::string term) {
    std::lock_guard<std::mutex> lock(index_jobs_mtx_);
    index_jobs_.emplace(std::move(term), anything::index_job_type::remove);
    if (index_jobs_.size() > batch_size_) {
        cv_.notify_one();
    }
}

void base_event_handler::worker_loop() {
    constexpr std::chrono::milliseconds timeout(2000);
    std::queue<anything::index_job> processing_jobs;

    for (;;) {
        // anything::log::info("entering wait");
        {
            std::unique_lock lock(index_jobs_mtx_);
            cv_.wait_for(lock, timeout, [this] { return index_jobs_.size() > batch_size_ || should_stop_; });

            if (should_stop_) {
                break;
            }

            std::swap(index_jobs_, processing_jobs);
        }

        // 调试输出，后续删除
        // if (!processing_jobs.empty()) {
        //     anything::log::info("processing jobs size: {}", processing_jobs.size());
        // }

        while (!processing_jobs.empty()) {
            auto job = std::move(processing_jobs.front());
            if (should_be_filtered(job.src)) {
                processing_jobs.pop();
                continue;
            }

            if (job.type == anything::index_job_type::add) {
                anything::log::debug("Indexed {}", job.src);
                {
                    std::lock_guard<std::mutex> lock(index_manager_mtx_);
                    index_manager_.add_index(std::move(job.src));
                }
               std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
            } else if (job.type == anything::index_job_type::remove) {
                anything::log::debug("Removed index: {}", job.src);
                std::lock_guard<std::mutex> lock(index_manager_mtx_);
                index_manager_.remove_index(job.src);
            }

            processing_jobs.pop();
        }
        // anything::log::info("worker end");
    }
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

    std::lock_guard<std::mutex> lock(index_manager_mtx_);
    return index_manager_.search(path, keywords, offset, max_count, true);
}

// 未特殊处理文件不存在的情况，只要最终不存在，就算成功
bool base_event_handler::removePath(const QString& fullPath) {
    auto path = fullPath.toStdString();

    std::lock_guard<std::mutex> lock(index_manager_mtx_);
    index_manager_.remove_index(path);
    return !index_manager_.document_exists(path);
}

bool base_event_handler::hasLFT(const QString& path) {
    std::lock_guard<std::mutex> lock(index_manager_mtx_);
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