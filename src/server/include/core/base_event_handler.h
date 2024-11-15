#ifndef ANYTHING_BASE_EVENT_HANDLER_H_
#define ANYTHING_BASE_EVENT_HANDLER_H_

#include <QObject>

#include "common/anything_fwd.hpp"
#include "common/fs_event.h"
#include "core/disk_scanner.h"
#include "core/file_index_manager.h"
#include "core/mount_manager.h"
#include "core/thread_pool.h"

ANYTHING_NAMESPACE_BEGIN

enum class index_job_type : char {
    add, remove, update
};

struct index_job {
    std::string src;
    std::optional<std::string> dst;
    index_job_type type;

    index_job(std::string src, index_job_type type, std::optional<std::string> dst = std::nullopt)
        : src(std::move(src)), dst(std::move(dst)), type(type) {}
};

ANYTHING_NAMESPACE_END

class base_event_handler : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "my.test.IAnything")

public:
    base_event_handler(std::string index_dir, QObject *parent = nullptr);
    virtual ~base_event_handler();

    virtual void handle(anything::fs_event event) = 0;

    void terminate_processing();

protected:
    void set_batch_size(std::size_t size);

    bool ignored_event(const std::string& path, bool ignored);

    void insert_pending_paths(std::vector<std::string> paths);

    std::size_t pending_paths_count() const;

    void refresh_mount_status();

    bool device_available(unsigned int device_id) const;

    std::string fetch_mount_point_for_device(unsigned int device_id) const;

    std::string get_index_directory() const;

    void set_index_change_filter(std::function<bool(const std::string&)> filter);

    void add_index_delay(std::string path);
    void remove_index_delay(std::string path);
    void update_index_delay(std::string src, std::string dst);

private:
    bool should_be_filtered(const std::string& path) const;

    void eat_jobs(std::vector<anything::index_job>& jobs, std::size_t number);
    void eat_job(const anything::index_job& job);

    void jobs_push(std::string src, anything::index_job_type type, std::optional<std::string> dst = std::nullopt);

    void timer_worker(int64_t interval);

public slots:
    /**
     * Searches for files containing the specified keyword within a given path,
     * starting from the specified offset, and returns a list of up to max_count results.
     *
     * @param path     The directory path to search within.
     * @param keyword  The keyword to search for in the files.
     * @param offset   The starting point in the result set for the search.
     * @param max_count The maximum number of results to return.
     * @return A QStringList containing the paths of the found files.
     */
    QStringList search(const QString& path, const QString& keywords, int offset, int max_count);
    
    bool removePath(const QString& fullPath);

    bool hasLFT(const QString& path);

    void addPath(const QString& fullPath);

    void index_files_in_directory(const QString& directory_path);

protected:
    anything::disk_scanner scanner_;

private:
    anything::mount_manager mnt_manager_;
    anything::file_index_manager index_manager_;
    std::size_t batch_size_;
    std::vector<std::string> pending_paths_;
    std::vector<anything::index_job> jobs_;
    std::function<bool(const std::string&)> index_change_filter_;
    anything::thread_pool pool_;
    std::thread timer_;
    std::mutex jobs_mtx_;
    std::mutex pending_mtx_;
    std::atomic<bool> stop_timer_{ false };
};

#endif // ANYTHING_BASE_EVENT_HANDLER_H_