#ifndef ANYTHING_BASE_EVENT_HANDLER_H_
#define ANYTHING_BASE_EVENT_HANDLER_H_

#include <QObject>

#include "anything/common/anything_fwd.hpp"
#include "anything/common/fs_event.h"
#include "anything/core/disk_scanner.h"
#include "anything/core/file_index_manager.h"
#include "anything/core/mount_manager.h"

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
    double multiply(double factor0, double factor2);
    double divide(double divident, double divisor);
    

signals:
    void newProduct(double product);
    void newQuotient(double quotient);

protected:
    anything::mount_manager mnt_manager_;
    anything::disk_scanner scanner_;
    anything::file_index_manager index_manager_;
};

#endif // ANYTHING_BASE_EVENT_HANDLER_H_