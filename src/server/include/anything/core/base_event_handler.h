#ifndef ANYTHING_BASE_EVENT_HANDLER_H_
#define ANYTHING_BASE_EVENT_HANDLER_H_

#include <QObject>

#include "anything/common/anything_fwd.hpp"
#include "anything/common/fs_event.h"
#include "anything/core/disk_scanner.h"
#include "anything/core/file_index_manager.h"
#include "anything/core/mount_manager.h"

class base_event_handler : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.deepin.IAnything")

public:
    base_event_handler(std::string index_dir, QObject* parent = nullptr);
    virtual ~base_event_handler();

    void process_documents_if_ready();

    virtual void handle(anything::fs_event event) = 0;

protected:
    bool ignored_event(const std::string& path, bool ignored);

public slots:
    void testMethod();

signals:
    void testSignal();

protected:
    anything::mount_manager mnt_manager_;
    anything::disk_scanner scanner_;
    anything::file_index_manager index_manager_;
};

#endif // ANYTHING_BASE_EVENT_HANDLER_H_