#ifndef ANYTHING_BASE_EVENT_HANDLER_H_
#define ANYTHING_BASE_EVENT_HANDLER_H_

#include <condition_variable>
#include <mutex>
#include <vector>
#include <thread>

#include <QObject>

#include "common/anything_fwd.hpp"
#include "common/fs_event.h"
#include "core/disk_scanner.h"
#include "core/file_index_manager.h"
#include "core/mount_manager.h"

ANYTHING_NAMESPACE_BEGIN

enum class index_job_type {
    add,
    remove,
    update
};

struct index_job {
    std::string src;
    std::optional<std::string> dst;
    index_job_type type;

    index_job(std::string src, index_job_type type, std::optional<std::string> dst = std::nullopt)
        : src{ std::move(src) }, dst{ std::move(dst) }, type{ type } {}
};

ANYTHING_NAMESPACE_END

class base_event_handler : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "my.test.IAnything")

public:
    base_event_handler(std::string index_dir, QObject *parent = nullptr);
    virtual ~base_event_handler();

    void terminate_processing();

    virtual void handle(anything::fs_event event) = 0;

    virtual void run_scheduled_task();

protected:
    void set_batch_size(std::size_t size);

    bool ignored_event(const std::string& path, bool ignored);

    void insert_pending_records(std::deque<anything::file_record> records);

    std::size_t record_size() const;

    void refresh_mount_status();

    bool device_available(unsigned int device_id) const;

    std::string fetch_mount_point_for_device(unsigned int device_id) const;

    std::string get_index_directory() const;

    void set_index_change_filter(std::function<bool(const std::string&)> filter);

    void add_index_delay(std::string path);
    void remove_index_delay(std::string term);

private:
    void worker_loop();

    bool should_be_filtered(const anything::file_record& record) const;
    bool should_be_filtered(const std::string& path) const;

public slots:
    // double multiply(double factor0, double factor2);
    // double divide(double divident, double divisor);
    
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
    QStringList search(
        const QString& path, const QString& keywords,
        int offset, int max_count);
    
    bool removePath(const QString& fullPath);

    bool hasLFT(const QString& path);

    void addPath(const QString& fullPath);

    void index_files_in_directory(const QString& directory_path);

signals:
    // void newProduct(double product);
    // void newQuotient(double quotient);
    // void newSearch(std::vector<anything::file_record> records);

protected:
    anything::disk_scanner scanner_;

private:
    anything::mount_manager mnt_manager_;
    anything::file_index_manager index_manager_;
    std::size_t batch_size_;
    std::mutex index_jobs_mtx_;
    std::mutex index_manager_mtx_;
    std::thread worker_;
    std::condition_variable cv_;
    bool should_stop_;
    std::deque<anything::file_record> records_;
    std::vector<anything::index_job> index_jobs_;
    std::function<bool(const std::string&)> index_change_filter_;
    // std::vector<Lucene::DocumentPtr> document_batch_;
};

#endif // ANYTHING_BASE_EVENT_HANDLER_H_