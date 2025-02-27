// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_BASE_EVENT_HANDLER_H_
#define ANYTHING_BASE_EVENT_HANDLER_H_

#include <QObject>
#include <QDBusAbstractAdaptor>

#include "common/anything_fwd.hpp"
#include "common/fs_event.h"
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
    Q_CLASSINFO("D-Bus Interface", "com.deepin.anything")

    Q_PROPERTY(bool autoIndexInternal READ autoIndexInternal WRITE setAutoIndexInternal NOTIFY autoIndexInternalChanged)
    Q_PROPERTY(bool autoIndexExternal READ autoIndexExternal WRITE setAutoIndexExternal NOTIFY autoIndexExternalChanged)

public:
    base_event_handler(std::string index_dir, QObject *parent = nullptr);
    virtual ~base_event_handler();

    virtual void handle(anything::fs_event event) = 0;

    void terminate_processing();

    // Do nothing
    bool autoIndexInternal() const { return true; }
    void setAutoIndexInternal(bool) {}
    bool autoIndexExternal() const { return true; }
    void setAutoIndexExternal(bool) {}

protected:
    void set_batch_size(std::size_t size);

    bool ignored_event(const std::string& path, bool ignored);

    void insert_pending_paths(std::vector<std::string> paths);
    void insert_index_directory(std::filesystem::path dir);

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
    QStringList search(const QString& path, QString keywords, int offset, int max_count);

    /**
     * Searches all files for a specified keyword and returns a list of matching file names.
     * 
     * @param keyword  The keyword to search for in the files.
     * @return A QStringList containing the paths of all files where the keyword is found.
     *         If no files are found, an empty list is returned.
     */
    QStringList search(const QString& path, QString keywords);

    QStringList search(const QString& path, QString keywords, const QString& type);

    QStringList search(const QString& path, QString keywords, const QString& after, const QString& before);

    QStringList traverse_directory(const QString& path);

    bool removePath(const QString& fullPath);

    bool hasLFT(const QString& path);
    QStringList hasLFTSubdirectories(QString path) const;

    void addPath(const QString& fullPath);

    void index_files_in_directory(const QString& directory_path);

    void delay_indexing(bool delay);

    QString cache_directory();

    /////////////////////////
    // Adapter

    QStringList search(int maxCount, qint64 icase, quint32 startOffset, quint32 endOffset,
                       const QString &path, QString keyword, bool useRegExp,
                       quint32 &startOffsetReturn, quint32 &endOffsetReturn);

    QStringList parallelsearch(const QString &path, quint32 startOffset, quint32 endOffset,
                               const QString &keyword, const QStringList &rules,
                               quint32 &startOffsetReturn, quint32 &endOffsetReturn);

    // Asynchronously search
    Q_NOREPLY void async_search(QString keywords);

Q_SIGNALS:
    void autoIndexInternalChanged(bool autoIndexInternal);
    void autoIndexExternalChanged(bool autoIndexExternal);
    void asyncSearchCompleted(const QStringList& results);

private:
    anything::mount_manager mnt_manager_;
    anything::file_index_manager index_manager_;
    std::size_t batch_size_;
    std::vector<std::string> pending_paths_;
    std::vector<anything::index_job> jobs_;
    std::function<bool(const std::string&)> index_change_filter_;
    anything::thread_pool pool_;
    std::atomic<bool> stop_timer_;
    std::mutex jobs_mtx_;
    std::mutex pending_mtx_;
    std::thread timer_;
    bool delay_mode_;
};

#endif // ANYTHING_BASE_EVENT_HANDLER_H_