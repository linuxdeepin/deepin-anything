#include "lft_manager.h"
#include "lftdisktool.h"
#include "mountcacher.h"
#include <iostream>
#include <QByteArray>
#include <QtConcurrent>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QVariantMap>
#include <dblockpartition.h>
#include <ddiskdevice.h>
#include <ddiskmanager.h>
#include <polkit-qt5-1/PolkitQt1/Authority>
#include <unistd.h> // getuid()


extern "C" {
#include "fs_buf.h"
#include "walkdir.h"
#include "resourceutil .h"
}

namespace anything {

lft_manager::lft_manager(QObject* parent)
    : QObject(parent)
    , settings_{lft_manager::cache_directory() + "/config.ini", QSettings::IniFormat}
{
    qDBusRegisterMetaType<QByteArrayList>();
    index_all(true);
}

lft_manager::~lft_manager()
{
}

bool lft_manager::add_path(QString path, bool auto_index)
{
    if (!check_authorization())
        return false;

    std::cout << "Add path: " << path.toStdString() << "\n";

    if (!path.startsWith("/")) {
        sendErrorReply(QDBusError::InvalidArgs, "The path should begin with a '/' character.");
        std::cerr << "The path should begin with a '/' character.\n";
        return false;
    }

    // 暂时不知道有何用
    // if (fswatcher_.contains(path)) {
    //     sendErrorReply(QDBusError::InternalError, "Index data building for this path");
    //     std::cerr << "Index data building for this path.\n";
    //     return false;
    // }
    const QByteArray& serial_uri = LFTDiskTool::pathToSerialUri(path);
    if (serial_uri.isEmpty()) {
        sendErrorReply(QDBusError::InvalidArgs, "Unable to convert the path to serial uri");
        std::cerr << "Unable to convert the path to serial uri\n";
        return false;
    }

    // 暂时不知道有何用
    // QFutureWatcher<fs_buf*> *watcher = new QFutureWatcher<fs_buf*>(this);
    // // 保存任务是否由自动索引触发的
    // watcher->setProperty("_d_autoIndex", autoIndex);
    
    // 此路径对应的设备可能被挂载到多个位置
    const QByteArrayList& path_list = LFTDiskTool::fromSerialUri(serial_uri);
    std::cout << "Equivalent paths: ";
    for (int i = 0; i < path_list.size(); ++i) {
        std::cout << path_list[i].constData();
        if (i != path_list.size() - 1) {
            std::cout << ", ";
        }
    }
    std::cout << "\n";

    // 将路径改为相对于第一个挂载点的路径，vfs_monitor中所有文件的改动都是以设备第一个挂载点通知的
    if (path_list.isEmpty())
        return false;
    path = path_list.first();

    // 信号和槽不起作用
    // 设置异步任务
    QFutureWatcher<fs_buf*>* watcher = new QFutureWatcher<fs_buf*>(this);
    watcher->setProperty("_d_autoIndex", auto_index);

    connect(watcher, &QFutureWatcher<fs_buf*>::finished,
        this, [] {
            std::cout << "\n\nFutureWatcher finished\n\n";
        }, Qt::QueuedConnection);

    QFuture<fs_buf*> result = QtConcurrent::run(
        lft_manager::build_fsbuf, watcher, path.endsWith('/') ? path : path + "/");
    watcher->setFuture(result);

    build_paths_.append(path);

    return false;
}

void lft_manager::add_path_by_partition(const DBlockDevice *block)
{
    std::cout << "device: " << block->device().toStdString()
              << " id: " << block->id().toStdString()
              << " drive: " << block->drive().toStdString()
              << "\n";
    
    if (auto* device = LFTDiskTool::diskManager()->createDiskDevice(block->drive())) {
        bool indexable = false;
        if (device->removable()) {
            indexable = settings_.value("autoIndexExternal", false).toBool();
            std::cout << "Removable device: " << device->path().toStdString() << "\n";
        } else {
            indexable = settings_.value("autoIndexInternal", true).toBool();
            std::cout << "Internal device: " << device->path().toStdString() << "\n";
        }

        // 建立索引时尽量使用根挂载点，这样索引的范围最大，同时与内核模块发出的事件的文件路径一致
        if (indexable) {
            auto mount_point = get_root_mount_point(block);
            std::cout << "Root mount point: " << mount_point.toStdString() << "\n";
            add_path(get_root_mount_point(block), true);
        }
        
        device->deleteLater();
    }
}

bool lft_manager::check_authorization()
{
    if (!calledFromDBus())
        return true;

    QString actionId("com.deepin.anything");
    QString appBusName = QDBusContext::message().service();
    PolkitQt1::Authority::Result result;

    result = PolkitQt1::Authority::instance()->checkAuthorizationSync(
        actionId, PolkitQt1::SystemBusNameSubject(appBusName),
        PolkitQt1::Authority::AllowUserInteraction);
    if (result == PolkitQt1::Authority::Yes) {
        return true;
    } else {
        if (calledFromDBus())
            QDBusContext::sendErrorReply(QDBusError::AccessDenied);
        return false;
    }
}

QString lft_manager::get_root_mount_point(const DBlockDevice *block)
{
    /* Perform conversions early to detect potential crashes as early as possible */
    QByteArrayList mountPointsByteArray = block->mountPoints();
    QStringList mountPoints;

    foreach (const QByteArray &item, mountPointsByteArray) {
        mountPoints.append(QString::fromLocal8Bit(item));
    }
    if(mountPoints.size() == 1) {
        return mountPoints.first();
    }

    QMap<QString, QString> mountPoint2Root = deepin_anything_server::MountCacher::instance()->getRootsByStrPoints(mountPoints);
    for (const QString &mountPoint : mountPoints) {
        if (mountPoint2Root.value(mountPoint) == "/") {
            return mountPoint;
        }
    }

    return mountPoints.first();
}

void lft_manager::index_all(bool force)
{
    std::cout << "Start building index\n";
    build_paths_.clear();

    // Traverse mounted partitions to check if index data needs to be built
    QVariantMap option;
    for (const QString& block : LFTDiskTool::diskManager()->blockDevices(option)) {
        if (!DBlockDevice::hasFileSystem(block))
            continue;
        
        DBlockDevice* device = DDiskManager::createBlockDevice(block);

        if (device->isLoopDevice())
            continue;

        if (device->mountPoints().isEmpty())
            continue;
        
        std::cout << "execution reached point index-all#1\n";
        if (force) {
            add_path_by_partition(device);
        } else {
            if (!lft_exists(QString::fromLocal8Bit(device->mountPoints().first())))
                add_path_by_partition(device);
            else
                std::cout << "The index data has already been created: "
                    << device->mountPoints().first().toStdString()
                    << ", block: " << block.toStdString() << "\n";
        }
    }


    std::cout << "End building index\n";
    std::cout << "------------------\n\n";
}

bool lft_manager::lft_exists(const QString &path)
{
    return !retrieve_fsbuf(path).first.isEmpty();
}

fs_buf* lft_manager::build_fsbuf(QFutureWatcherBase* futureWatcher, const QString& path)
{
    std::cout << "build_fsbuf() begin----------------\n";
    fs_buf *buf = new_fs_buf(1 << 24, path.toLocal8Bit().constData());

    if (!buf)
        return buf;

    if (build_fstree(buf, false, handle_build_fs_buf_progress, futureWatcher) != 0) {
        free_fs_buf(buf);
        std::cerr << "[LFT] Failed on build fs buffer of path: " << path.toStdString() << "\n";
        return nullptr;
    }

    if (!check_fsbuf(buf)) {
        free_fs_buf(buf);
        std::cout << "[LFT] Failed on check fs buffer of path: " << path.toStdString() << "\n";
        return nullptr;
    }

    std::cout << "build_fsbuf() end----------------\n";
    return buf;
}

QString lft_manager::cache_directory()
{
    QString cachePath("/var/cache/deepin/deepin-anything");

    if (getuid() != 0 && !QFileInfo(cachePath).isWritable()) {
        cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

        if (cachePath.isEmpty() || cachePath == "/") {
            cachePath = QString("/tmp/deepin/deepin-anything");
        }
    }

    std::cout << "Cache Dir:" << cachePath.toStdString() << "\n";
    if (!QDir::home().mkpath(cachePath)) {
        std::cerr << "Failed to create cache path.\n";
    }

    return cachePath;
}

bool lft_manager::check_fsbuf(fs_buf *buf)
{
    return get_tail(buf) != first_name(buf);
}

int lft_manager::handle_build_fs_buf_progress(uint32_t file_count, uint32_t dir_count, const char *cur_dir, const char *cur_file, void *param)
{
    Q_UNUSED(file_count)
    Q_UNUSED(dir_count)
    Q_UNUSED(cur_dir)
    Q_UNUSED(cur_file)

    QFutureWatcherBase *futureWatcher = static_cast<QFutureWatcherBase*>(param);
    // 在并行里立即检测watcher, 它的状态可能未更新(isStarted=true 但是isRunning=false && isFinished=true)
    // BUG93215，导致索引建立被取消
    if (!futureWatcher || !futureWatcher->isRunning()) {
        return 0;
    }

    if (futureWatcher->isCanceled()) {
        return 1;
    }

    return 0;
}

QPair<QString, fs_buf*> lft_manager::retrieve_fsbuf(const QString &path)
{
    if (!path.startsWith("/"))
        return QPair<QString, fs_buf*>();
    
    // 获取路径挂载的真实设备挂载点，比如长文件名挂载点"/data/home/user/Documents"的设备挂载点是"/data"
    QString mountPoint = deepin_anything_server::MountCacher::instance()->findMountPointByPath(path, true);
    if (mountPoint.isEmpty()) {
        std::cerr << "Failed to find mount point for path: " << path.toStdString() << "\n";
        return QPair<QString, fs_buf*>();
    }

    QPair<QString, fs_buf*> buf_pair;
    fs_buf* buf = fsbuf_.value(mountPoint);
    if (buf) {
        // path相对于此fs_buf root_path的路径
        QString new_path = path.mid(mountPoint.size());

        // 移除多余的 / 字符
        if (new_path.startsWith("/"))
            new_path = new_path.mid(1);
        
        // fs_buf中的root_path以/结尾，所以此处直接拼接
        new_path.prepend(QString::fromLocal8Bit(get_root_path(buf)));

        // 移除多余的 / 字符
        if (new_path.size() > 1 && new_path.endsWith("/"))
            new_path.chop(1);

        buf_pair = qMakePair(new_path, buf);
    }

    return buf_pair;
}

} // namespace anything