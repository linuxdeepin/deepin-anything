#include "core/base_event_handler.h"

#include "common/file_record.hpp"
#include "utils/log.h"
#include "utils/string_helper.h"
#include <anythingadaptor.h>

base_event_handler::base_event_handler(std::string index_dir, QObject *parent)
    : QObject(parent), index_manager_(std::move(index_dir)), batch_size_(100),
      timer_(std::thread(&base_event_handler::timer_worker, this, 1000)) {
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
    if (!jobs_.empty()) {
        // Eat all jobs
        for (auto&& job : jobs_) {
            eat_job(std::move(job));
        }
    }
}

void base_event_handler::terminate_processing() {
    stop_timer_ = true;

    if (timer_.joinable()) {
        auto thread_id = timer_.get_id();
        timer_.join();
        anything::log::debug("Timer thread {} has exited", thread_id);
    }
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

void base_event_handler::insert_pending_paths(
    std::vector<std::string> paths) {
    std::lock_guard<std::mutex> lock(pending_mtx_);
    pending_paths_.insert(pending_paths_.end(),
                    std::make_move_iterator(paths.begin()),
                    std::make_move_iterator(paths.end()));
}

std::size_t base_event_handler::pending_paths_count() const {
    return pending_paths_.size();
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
    jobs_push(std::move(path), anything::index_job_type::add);
}

void base_event_handler::remove_index_delay(std::string path) {
    jobs_push(std::move(path), anything::index_job_type::remove);
}

void base_event_handler::update_index_delay(std::string src_path, std::string dst_path) {
    jobs_push(std::move(src_path), anything::index_job_type::update, std::move(dst_path));
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

void base_event_handler::eat_jobs(std::vector<anything::index_job>& jobs, std::size_t number) {
    std::vector<anything::index_job> processing_jobs;
    processing_jobs.insert(
        processing_jobs.end(),
        std::make_move_iterator(jobs.begin()),
        std::make_move_iterator(jobs.begin() + number));
    jobs.erase(jobs.begin(), jobs.begin() + number);
    pool_.enqueue_detach([this, processing_jobs = std::move(processing_jobs)]() {
        for (const auto& job : processing_jobs) {
            eat_job(job);
        }
    });
}

void base_event_handler::eat_job(const anything::index_job& job) {
    if (job.type == anything::index_job_type::add) {
        index_manager_.add_index(job.src);
    } else if (job.type == anything::index_job_type::remove) {
        index_manager_.remove_index(job.src);
    } else if (job.type == anything::index_job_type::update) {
        if (job.dst) {
            index_manager_.update_index(job.src, *job.dst);
        }
    }
}

void base_event_handler::jobs_push(std::string path, anything::index_job_type type,
    std::optional<std::string> dst) {
    if (should_be_filtered(path) || (dst && should_be_filtered(*dst))) {
        return;
    }

    std::lock_guard<std::mutex> lock(jobs_mtx_);
    jobs_.emplace_back(std::move(path), type, std::move(dst));
    if (jobs_.size() >= batch_size_) {
        eat_jobs(jobs_, batch_size_);
    }
}

void base_event_handler::timer_worker(int64_t interval) {
    // pending batch size 小时，CPU Usage 低，全部索引建立完成的时间长
    // pending batch size 大时，CPU Usage 高，全部索引建立完成的时间短
    constexpr std::size_t pending_batch_size = 100;
    while(!stop_timer_) {
        // {
        //     std::lock_guard<std::mutex> lock(jobs_mtx_);
        //     if (!jobs_.empty()) {
        //         eat_jobs(jobs_, std::min(batch_size_, jobs_.size()));
        //     }
        // }
        // std::this_thread::sleep_for(std::chrono::milliseconds(interval));


        bool idle = false;
        {
            std::lock_guard<std::mutex> lock(jobs_mtx_);
            if (!jobs_.empty()) {
                eat_jobs(jobs_, std::min(batch_size_, jobs_.size()));
            } else {
                idle = true;
            }
        }

        // 无任务时自动索引未存在的系统文件，维护系统完整性
        if (idle) {
            std::vector<std::string> path_batch;
            {
                std::lock_guard<std::mutex> lock(pending_mtx_);
                if (!pending_paths_.empty()) {
                    std::size_t batch_size = std::min(pending_batch_size, pending_paths_.size());
                    path_batch.insert(
                        path_batch.end(),
                        std::make_move_iterator(pending_paths_.begin()),
                        std::make_move_iterator(pending_paths_.begin() + batch_size));
                    pending_paths_.erase(pending_paths_.begin(), pending_paths_.begin() + batch_size);
                }
            }

            anything::log::debug("path batch size: {}", path_batch.size());
            for (auto&& path : path_batch) {
                if (should_be_filtered(path)) {
                    continue;
                }

                // TD: 插入前还需要检查文件是否本地存在，以避免新任务刚删除了索引，这里又插入了该索引
                // 此处 document_exists 不需要是实时的，只需要是程序刚启动时的状态，以避免线程同步带来的效率问题
                // 因为已存在却未建索引的文件，不会产生新的插入事件，所以只需和初始状态对比即可
                // 索引完整性只考虑插入，这里无法考虑删除，逐个检查已建索引是否本地存在效率会很低，放到查询时处理
                // ! 已完成此 TD !
                if (!index_manager_.document_exists(path, true)) {
                    if (std::filesystem::exists(path)) {
                        add_index_delay(std::move(path));
                    }
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
    }
}

QStringList base_event_handler::search(
    const QString& path, const QString& keywords,
    int offset, int max_count) {
    if (offset < 0) {
        return {};
    }

    // TD: 由于没有自动清理无效索引，系统中可能含有一些实际不存在的文件路径
    // 在此可过滤并清除无效索引，维护索引的有效性
    // ! 已完成此 TD !
    QStringList results = index_manager_.search(path, keywords, offset, max_count, true);
    int old_results_count = results.size();
    if (!results.empty()) {
        // anything::log::debug("result size: {}", results.size());
        // 清除非真实文件
        std::vector<std::string> remove_list;
        for (int i = 0; i < results.size();) {
            std::string path = results[i].toStdString();
            if (!std::filesystem::exists(path)) {
                remove_list.push_back(std::move(path));
                results.removeAt(i);
            } else {
                ++i;
            }
        }

        // 说明还存在更多结果，继续向后搜索
        if (old_results_count == max_count) {
            if (!remove_list.empty()) {
                // anything::log::debug("remove size: {}", remove_list.size());
                results.append(search(path, keywords, offset + max_count, remove_list.size()));
            }
        }
        for (auto&& path : remove_list) {
            remove_index_delay(std::move(path));
        }
    }
    return results;
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