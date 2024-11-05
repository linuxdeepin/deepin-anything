#ifndef ANYTHING_BASE_EVENT_HANDLER_H_
#define ANYTHING_BASE_EVENT_HANDLER_H_

#include <QObject>

#include "common/anything_fwd.hpp"
#include "common/fs_event.h"
#include "core/disk_scanner.h"
#include "core/file_index_manager.h"
#include "core/mount_manager.h"

class base_event_handler : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "my.test.IAnything")

public:
    base_event_handler(std::string index_dir, QObject *parent = nullptr);
    virtual ~base_event_handler();

    void process_documents_if_ready();

    virtual void handle(anything::fs_event event) = 0;

    virtual void run_scheduled_task();

protected:
    bool ignored_event(const std::string& path, bool ignored);

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

    bool addPath(const QString& fullPath);

signals:
    // void newProduct(double product);
    // void newQuotient(double quotient);
    // void newSearch(std::vector<anything::file_record> records);

protected:
    anything::mount_manager mnt_manager_;
    anything::disk_scanner scanner_;
    anything::file_index_manager index_manager_;
};

#endif // ANYTHING_BASE_EVENT_HANDLER_H_