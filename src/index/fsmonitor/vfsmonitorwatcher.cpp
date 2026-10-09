// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vfsmonitorwatcher_p.h"
#include "event_relay_receiver.h"

#include <QDir>
#include <QFileInfo>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>

#include <libmount.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <QDebug>

ANYTHING_INDEX_BEGIN_NAMESPACE

namespace {

constexpr char kAnythingBusName[] = "org.deepin.Anything";
constexpr char kAnythingObjectPath[] = "/org/deepin/Anything";
constexpr char kAnythingInterfaceName[] = "org.deepin.Anything";

constexpr size_t kDispatchMaxPathLen = 4096;

// Upper bound of events turned into signals per home-thread wakeup so a huge
// backlog cannot starve the event loop for seconds.
constexpr int kMaxEventsPerDrain = 2048;

struct MountEntry
{
    dev_t deviceId { 0 };
    int parentMountId { 0 };
    QString mountPoint;
    bool isBindMount { false };
    bool isLowerFs { false };
};

struct fs_event
{
    guint8      act;
    guint32     cookie;
    guint16     major;
    guint32     minor;
    gchar       path[kDispatchMaxPathLen];
};

struct FileIdentity
{
    dev_t deviceId { 0 };
    ino_t inode { 0 };
    bool valid { false };
};

bool isDescendantOfRoot(const QString &path, const QString &root)
{
    if (root == "/")
        return path.startsWith('/');

    if (!path.startsWith(root))
        return false;

    if (path.length() == root.length())
        return false;

    return root.endsWith('/') || path.at(root.length()) == '/';
}

bool mountPointStartsWith(const QString &path, const QString &mountPoint)
{
    if (!path.startsWith(mountPoint))
        return false;

    return mountPoint.endsWith('/') || path.length() == mountPoint.length()
            || path.at(mountPoint.length()) == '/';
}

bool cStringEquals(const char *left, const char *right)
{
    return left && right && qstrcmp(left, right) == 0;
}

bool isLowerFsType(const char *fsType)
{
    return cStringEquals(fsType, "overlay") || cStringEquals(fsType, "fuse.dlnfs")
            || cStringEquals(fsType, "ulnfs");
}

bool isParentChainUnderRoot(const QHash<int, MountEntry> &byMountId, const MountEntry &entry)
{
    if (!entry.isBindMount && entry.mountPoint == "/")
        return true;

    int parentMountId = entry.parentMountId;
    while (parentMountId > 0) {
        auto parentIt = byMountId.find(parentMountId);
        if (parentIt == byMountId.end())
            return false;

        if (parentIt->mountPoint == "/")
            return true;

        parentMountId = parentIt->parentMountId;
    }

    return false;
}

QHash<int, MountEntry> collectMountEntries(libmnt_table *mtab)
{
    QHash<int, MountEntry> byMountId;
    libmnt_iter *iter = mnt_new_iter(MNT_ITER_FORWARD);
    if (!iter)
        return byMountId;

    libmnt_fs *fs = nullptr;
    while (mnt_table_next_fs(mtab, iter, &fs) == 0) {
        const char *target = mnt_fs_get_target(fs);
        if (!target)
            continue;

        MountEntry entry;
        entry.deviceId = mnt_fs_get_devno(fs);
        entry.parentMountId = mnt_fs_get_parent_id(fs);
        entry.mountPoint = QString::fromUtf8(target);
        entry.isBindMount = !cStringEquals(mnt_fs_get_root(fs), "/");
        entry.isLowerFs = isLowerFsType(mnt_fs_get_fstype(fs));

        byMountId.insert(mnt_fs_get_id(fs), entry);
    }

    mnt_free_iter(iter);
    return byMountId;
}

}   // anonymous namespace

// ========== VfsSocketReader ==========

// Lives in a dedicated QThread. Its only job is to drain the dispatcher
// socket
// as fast as the kernel delivers packets and park the events in the
// userspace queue owned by VfsMonitorFileSystemWatcherPrivate.
//
// Why a dedicated thread: the dispatcher kicks any client whose kernel
// receive buffer overflows (send() -> EAGAIN -> "slow client ... kicking",
// see deepin-anything src/dispatcher/event_relay_dispatcher.c). Kernel
// buffers are capped by net.core.rmem_max / wmem_max (~416 KiB ≈ ~100
// packets of 4 KB), so no setsockopt can absorb a burst of thousands — let
// alone 300k files — if draining depends on how fast events are processed.
// Mirrors the daemon's own event_listener (dedicated thread + draining loop,
// deepin-anything commit f2dd210): keep the read path tiny and buffer in
// userspace.
//
// Because the relay dispatcher closes the connection on the very first
// EAGAIN, this hot loop must stay as cheap as physically possible:
//   - recv() reads one fs_event per syscall (SOCK_SEQPACKET preserves
//     message boundaries); the fd is non-blocking so EAGAIN stops the drain;
//   - received slots are never re-zeroed (a 4 KiB memset per packet); each
//     path is NUL-terminated in place at the path field boundary;
//   - mount/unmount notifications only set a flag — the mount-table refresh
//     (parsing /proc/self/mountinfo plus alias stat()s) runs once after the
//     drain loop instead of stalling it mid-burst;
//   - resolved events are parked in a reusable batch vector and pushed into
//     the userspace queue under a single lock acquisition.
class VfsSocketReader final : public QObject
{
public:
    explicit VfsSocketReader(VfsMonitorFileSystemWatcherPrivate *dd)
        : d(dd)
    {
    }

    // Runs in the reader thread. Adopts a connected fd and starts watching.
    void begin(int fd)
    {
        d->pendingFd.storeRelaxed(-1);   // ownership transferred

        if (notifier) {
            notifier->setEnabled(false);
            notifier->deleteLater();
            notifier = nullptr;
        }
        if (socketFd >= 0 && socketFd != fd)
            ::close(socketFd);

        socketFd = fd;
        notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        QObject::connect(notifier, &QSocketNotifier::activated, notifier, [this]() {
            drainSocket();
        });
    }

    // Runs in the reader thread. Releases the socket and the notifier.
    void shutdown()
    {
        if (notifier) {
            notifier->setEnabled(false);
            delete notifier;
            notifier = nullptr;
        }

        if (socketFd >= 0) {
            ::close(socketFd);
            socketFd = -1;
        }
    }

private:
    // Runs in the reader thread (QSocketNotifier callback): drain everything
    // the kernel has buffered, then return. Level-triggered, so a still-full
    // buffer re-arms the notifier.
    void drainSocket()
    {
        constexpr size_t kPathOffset = offsetof(fs_event, path);
        constexpr size_t kPathLen = sizeof(fs_event().path);

        while (socketFd >= 0) {
            // SOCK_SEQPACKET preserves message boundaries: one recv() returns
            // one complete fs_event. The fd is non-blocking (set by the relay
            // dispatcher's socketpair), so EAGAIN stops the drain.
            const ssize_t received = ::recv(socketFd, &receiveSlot,
                                             sizeof(receiveSlot), 0);
            if (received < 0) {
                if (errno == EINTR)
                    continue;

                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;   // fully drained for now

                qWarning() << "VfsMonitor: failed to receive dispatcher event:" << std::strerror(errno);
                flushPendingBatch();
                breakConnection();
                return;
            }

            if (received == 0) {
                // SEQPACKET EOF: the dispatcher dispatcher closed the connection.
                qWarning() << "VfsMonitor: event dispatcher connection closed";
                flushPendingBatch();
                breakConnection();
                return;
            }

            if (static_cast<size_t>(received) < kPathOffset + 1) {
                qWarning() << "VfsMonitor: received short dispatcher message:" << received;
                continue;
            }

            fs_event &event = receiveSlot;

            // The server sends sizeof(fs_event) bytes with the path field
            // NUL-terminated by g_strlcpy. Terminate at the path boundary
            // defensively in case a short message left the tail uninitialised.
            const size_t pathBytes = static_cast<size_t>(received) - kPathOffset;
            event.path[std::min(pathBytes, kPathLen) - 1] = '\0';

            dev_t deviceId = makedev(event.major, event.minor);

            const int act = event.act;
            if (act < ACT_NEW_FILE || act > ACT_CLOSE_WRITE_FILE)
                continue;

            if (act == ACT_MOUNT || act == ACT_UNMOUNT) {
                // Refreshing the mount table (mtab parse + alias stats)
                // inside this loop would stall the drain mid-burst long
                // enough for the dispatcher to overflow and kick us;
                // coalesce and run it once after the loop instead.
                mountRefreshPending = true;
                continue;
            }

            // RENAME_TO is always forwarded: an unresolved destination
            // means "renamed out of the monitored roots" for the paired
            // RENAME_FROM, and a missing pair means "created here"
            // (kept semantics).
            if (act == ACT_RENAME_TO_FILE || act == ACT_RENAME_TO_FOLDER) {
                const QString resolved = d->resolveAndFilterFullPath(deviceId, event.path);
                pendingBatch.append(QueuedFsEvent { act, event.cookie, QString(), resolved });
                continue;
            }

            const QString resolved = d->resolveAndFilterFullPath(deviceId, event.path);
            if (resolved.isNull())
                continue;

            pendingBatch.append(QueuedFsEvent { act, event.cookie, resolved, QString() });
        }

        flushPendingBatch();

        if (mountRefreshPending) {
            mountRefreshPending = false;
            if (!d->initMountPoints())
                qWarning() << "VfsMonitor: failed to refresh mount point aliases";
        }

        d->scheduleEventDrain();
    }

    // Pushes the events collected since the last flush into the userspace
    // queue in one locked pass (dropping on overflow — never blocking).
    // Runs in the reader thread.
    void flushPendingBatch()
    {
        if (pendingBatch.isEmpty())
            return;

        {
            QMutexLocker locker(&d->queueMutex);
            for (QueuedFsEvent &event : pendingBatch) {
                if (d->eventQueue.size() >= d->maxQueuedEvents) {
                    if (!d->overflowFlag.fetchAndStoreRelaxed(1)) {
                        qWarning() << "VfsMonitor: event queue full (" << d->maxQueuedEvents
                                    << "), dropping events until drained";
                    }
                    break;
                }
                d->eventQueue.enqueue(std::move(event));
            }
        }
        pendingBatch.clear();

        d->scheduleEventDrain();
    }

    // Runs in the reader thread. The notifier must be disabled and destroyed
    // via deleteLater because this is called from inside its activated()
    // signal.
    void breakConnection()
    {
        if (notifier) {
            notifier->setEnabled(false);
            notifier->deleteLater();
            notifier = nullptr;
        }

        if (socketFd >= 0) {
            ::close(socketFd);
            socketFd = -1;
        }

        // Everything delivered while the connection is down is lost; the
        // service compensates with a full update task (eventsLost fallback).
        Q_EMIT d->q_ptr->eventsLost();
        QMetaObject::invokeMethod(d->q_ptr, [d = d]() { d->handleDisconnect(); }, Qt::QueuedConnection);
    }

    VfsMonitorFileSystemWatcherPrivate *d;
    QSocketNotifier *notifier { nullptr };
    int socketFd { -1 };

    // Reused receive buffer: one recv() reads one fs_event (SOCK_SEQPACKET
    // preserves message boundaries).
    fs_event receiveSlot {};
    // Events decoded since the last flush, parked in reader-thread-owned
    // storage so the userspace queue is filled under one lock per batch.
    QVector<QueuedFsEvent> pendingBatch;
    bool mountRefreshPending { false };
};

// ========== VfsMonitorFileSystemWatcherPrivate ==========

VfsMonitorFileSystemWatcherPrivate::VfsMonitorFileSystemWatcherPrivate(
        const QStringList &rootPaths,
        VfsMonitorFileSystemWatcher::PathExcludePredicate excludePredicate,
        VfsMonitorFileSystemWatcher *qq)
    : q_ptr(qq), excludePredicate(std::move(excludePredicate))
{
    this->rootPaths.reserve(rootPaths.size());
    for (const QString &path : rootPaths) {
        this->rootPaths.append(QDir(path).absolutePath());
    }
    this->rootPaths.removeDuplicates();
}

VfsMonitorFileSystemWatcherPrivate::~VfsMonitorFileSystemWatcherPrivate()
{
    if (reconnectTimer) {
        reconnectTimer->stop();
    }

    if (readerThread) {
        if (readerThread->isRunning() && reader) {
            // Ensure the notifier and the fd owned by the reader thread are
            // released before the thread is torn down.
            QMetaObject::invokeMethod(reader, [r = reader]() { r->shutdown(); },
                                      Qt::BlockingQueuedConnection);
        }
        readerThread->quit();
        readerThread->wait();
    }

    delete reader;
    reader = nullptr;

    // Close an fd that was connected but never adopted by the reader thread.
    const int fd = pendingFd.fetchAndStoreRelaxed(-1);
    if (fd >= 0)
        ::close(fd);
}

bool VfsMonitorFileSystemWatcherPrivate::initMountPoints()
{
    mountPoints.clear();
    childMountPoints.clear();
    lowerFsExists = false;

    struct libmnt_table *mtab = mnt_new_table();
    if (!mtab) {
        return false;
    }

    if (mnt_table_parse_mtab(mtab, nullptr) < 0) {
        mnt_free_table(mtab);
        return false;
    }

    const QHash<int, MountEntry> byMountId = collectMountEntries(mtab);
    mnt_free_table(mtab);

    QHash<int, MountEntry> rootMountTree;
    for (auto it = byMountId.cbegin(); it != byMountId.cend(); ++it) {
        const auto &entry = it.value();

        if (!isParentChainUnderRoot(byMountId, entry))
            continue;

        mountPoints[entry.deviceId].append(entry.mountPoint);
        rootMountTree.insert(it.key(), entry);
        lowerFsExists = lowerFsExists || entry.isLowerFs;
    }

    for (auto &points : mountPoints) {
        std::sort(points.begin(), points.end(),
                  [](const QString &a, const QString &b) {
                      return a.length() > b.length();
                  });
    }

    for (auto it = rootMountTree.cbegin(); it != rootMountTree.cend(); ++it) {
        const auto &parent = it.value();
        QStringList children;
        for (const auto &entry : std::as_const(rootMountTree)) {
            if (entry.parentMountId == it.key()) {
                children.append(entry.mountPoint);
            }
        }

        if (!children.isEmpty())
            childMountPoints[parent.deviceId].append(children);
    }

    for (auto &points : childMountPoints) {
        points.removeDuplicates();
        std::sort(points.begin(), points.end(),
                  [](const QString &a, const QString &b) {
                      return a.length() > b.length();
                  });
    }

    int totalPoints = 0;
    for (const auto &pts : std::as_const(mountPoints)) {
        totalPoints += pts.size();
    }
    qInfo() << "VfsMonitor: loaded" << mountPoints.size()
            << "devices," << totalPoints << "mount points,"
            << childMountPoints.size() << "devices with child mount points,"
            << "lowerfs exists:" << lowerFsExists;
    return !mountPoints.isEmpty();
}

bool VfsMonitorFileSystemWatcherPrivate::isLowerFsEvent(dev_t deviceId, const QString &fullPath) const
{
    if (!lowerFsExists)
        return false;

    auto it = childMountPoints.find(deviceId);
    if (it == childMountPoints.end())
        return false;

    for (const QString &childMountPoint : it.value()) {
        if (mountPointStartsWith(fullPath, childMountPoint)) {
            return true;
        }
    }

    return false;
}

QString VfsMonitorFileSystemWatcherPrivate::resolveAndFilterFullPath(dev_t deviceId,
                                                                     const char *relativePath) const
{
    auto it = mountPoints.find(deviceId);
    if (it == mountPoints.end())
        return {};

    const QStringList &points = it.value();
    const QString relPath = QString::fromUtf8(relativePath);

    // Try each mount point (sorted longest first).
    // Return the first one that falls under a monitored root path
    // and passes the exclude predicate.
    for (const QString &mp : points) {
        QString fullPath = (mp == "/") ? relPath : (mp + relPath);

        if (isLowerFsEvent(deviceId, fullPath))
            continue;

        if (std::none_of(rootPaths.cbegin(), rootPaths.cend(),
                         [&fullPath](const QString &root) { return isDescendantOfRoot(fullPath, root); }))
            continue;

        if (excludePredicate && excludePredicate(fullPath))
            continue;

        return fullPath;
    }

    // In overlay root environments, the root "/" filesystem has no separate mount
    // entry in mountPoints. When all mount points fail and relPath is already
    // an absolute path, treat it as the true absolute path and re-check.
    if (relPath.startsWith('/')) {
        auto found = std::find_if(rootPaths.cbegin(), rootPaths.cend(),
                                  [&relPath, this](const QString &root) {
                                      return isDescendantOfRoot(relPath, root)
                                              && (!excludePredicate || !excludePredicate(relPath));
                                  });
        if (found != rootPaths.cend())
            return relPath;
    }

    return {};
}

QPair<QString, QString> VfsMonitorFileSystemWatcherPrivate::splitPath(const QString &fullPath)
{
    QFileInfo fi(fullPath);
    return qMakePair(fi.absolutePath(), fi.fileName());
}

int VfsMonitorFileSystemWatcherPrivate::connectDispatcherSocket()
{
    // Obtain the event channel fd via D-Bus fd passing from the
    // org.deepin.Anything service's GetEventChannel method.
    EventRelayReceiver *receiver = event_relay_receiver_new(
        kAnythingBusName, kAnythingObjectPath, kAnythingInterfaceName);
    if (!receiver) {
        qWarning() << "VfsMonitor: failed to create event relay receiver";
        return -1;
    }

    int fd = -1;
    guint32 protocol_id = 0;
    if (!event_relay_receiver_get_fd(receiver, &fd, &protocol_id)) {
        qWarning() << "VfsMonitor: failed to get fd from relay receiver";
        event_relay_receiver_free(receiver);
        return -1;
    }

    // The receiver owns the fd and closes it on free. Dup so the reader
    // thread owns its own fd, then free the receiver.
    fd = ::dup(fd);
    event_relay_receiver_free(receiver);

    if (fd < 0) {
        qWarning() << "VfsMonitor: failed to dup relay fd:" << std::strerror(errno);
        return -1;
    }

    // Enlarge the receive buffer for burst headroom.
    constexpr int kReceiveBufSize = 8 << 20;
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &kReceiveBufSize,
                     sizeof(kReceiveBufSize)) < 0) {
        qDebug() << "VfsMonitor: setsockopt(SO_RCVBUF) failed:" << std::strerror(errno);
    }

    return fd;
}

void VfsMonitorFileSystemWatcherPrivate::startReaderThread(int fd)
{
    reader = new VfsSocketReader(this);
    readerThread = new QThread(q_ptr);
    reader->moveToThread(readerThread);
    pendingFd.storeRelaxed(fd);
    readerThread->start();
    QMetaObject::invokeMethod(reader, [this, fd]() { reader->begin(fd); }, Qt::QueuedConnection);
}

bool VfsMonitorFileSystemWatcherPrivate::initDispatcher()
{
    Q_Q(VfsMonitorFileSystemWatcher);

    if (!initMountPoints()) {
        qWarning() << "VfsMonitor: failed to initialize mount point aliases";
    }

    // Reconnect timer lives on the home thread (the thread that called
    // create()). It is single-shot and rearmed by attemptReconnect().
    reconnectTimer = new QTimer(q);
    reconnectTimer->setSingleShot(true);
    QObject::connect(reconnectTimer, &QTimer::timeout, q, [this]() {
        attemptReconnect();
    });
    reconnectBackoffMs = 0;

    // Queue capacity override for tests and tuning.
    bool ok = false;
    const int envQueue = qEnvironmentVariableIntValue("DFM_VFSMONITOR_MAX_QUEUE", &ok);
    if (ok && envQueue > 0)
        maxQueuedEvents = envQueue;

    const int fd = connectDispatcherSocket();
    if (fd < 0) {
        // Initial connection failed: the dispatcher is not running yet.
        // Return false so create() reports the watcher as unavailable and
        // FSMonitorPrivate degrades to inotify-only mode. The auto-reconnect
        // timer created above only self-heals connections that were
        // established at runtime and then dropped; it cannot help here
        // because create() deletes this watcher on a failed first connect.
        return false;
    }

    startReaderThread(fd);

    qInfo() << "VfsMonitor: connected to deepin-anything event dispatcher";
    return true;
}

void VfsMonitorFileSystemWatcherPrivate::scheduleEventDrain()
{
    if (!drainScheduled.testAndSetRelaxed(0, 1))
        return;

    QMetaObject::invokeMethod(q_ptr, [this]() { drainQueuedEvents(); }, Qt::QueuedConnection);
}

void VfsMonitorFileSystemWatcherPrivate::drainQueuedEvents()
{
    Q_Q(VfsMonitorFileSystemWatcher);

    // Re-open the gate before checking the queue so an event enqueued while
    // this drain runs cannot be lost (see scheduleEventDrain).
    drainScheduled.storeRelaxed(0);

    QVector<QueuedFsEvent> batch;
    {
        QMutexLocker locker(&queueMutex);
        while (!eventQueue.isEmpty() && batch.size() < kMaxEventsPerDrain)
            batch.append(eventQueue.dequeue());
    }

    if (!batch.isEmpty()) {
        for (const QueuedFsEvent &event : std::as_const(batch))
            dispatchQueuedEvent(event);

        // Clean up orphaned RENAME_FROM entries.
        static constexpr int kPendingRenameCleanupThreshold = 1000;
        if (pendingRenames.size() > kPendingRenameCleanupThreshold) {
            qWarning() << "VfsMonitor: pending rename table too large ("
                        << pendingRenames.size() << "), clearing";
            pendingRenames.clear();
        }
    }

    {
        QMutexLocker locker(&queueMutex);
        if (!eventQueue.isEmpty()) {
            scheduleEventDrain();
        }
    }

    if (overflowFlag.testAndSetRelaxed(1, 0)) {
        qWarning() << "VfsMonitor: userspace event queue overflowed, filesystem events were dropped";
        Q_EMIT q->eventsLost();
    }
}

void VfsMonitorFileSystemWatcherPrivate::dispatchQueuedEvent(const QueuedFsEvent &event)
{
    auto *q = q_ptr;
    const int act = event.action;

    if (act == ACT_RENAME_FROM_FILE || act == ACT_RENAME_FROM_FOLDER) {
        auto [parentPath, name] = splitPath(event.pathA);
        RenameFromInfo info;
        info.path = parentPath;
        info.name = name;
        info.isDirectory = (act == ACT_RENAME_FROM_FOLDER);
        pendingRenames.insert(event.cookie, info);
        return;
    }

    if (act == ACT_RENAME_TO_FILE || act == ACT_RENAME_TO_FOLDER) {
        const bool isDir = (act == ACT_RENAME_TO_FOLDER);
        auto it = pendingRenames.find(event.cookie);
        if (it != pendingRenames.end()) {
            if (!event.pathB.isEmpty()) {
                auto [parentPath, name] = splitPath(event.pathB);
                if (isDir)
                    Q_EMIT q->directoryMoved(it->path, it->name, parentPath, name);
                else
                    Q_EMIT q->fileMoved(it->path, it->name, parentPath, name);
            } else {
                // Destination outside the monitored roots: the source
                // effectively disappeared from the index.
                if (isDir)
                    Q_EMIT q->directoryDeleted(it->path, it->name);
                else
                    Q_EMIT q->fileDeleted(it->path, it->name);
            }
            pendingRenames.erase(it);
        } else if (!event.pathB.isEmpty()) {
            auto [parentPath, name] = splitPath(event.pathB);
            if (isDir)
                Q_EMIT q->directoryCreated(parentPath, name);
            else
                Q_EMIT q->fileCreated(parentPath, name);
        }
        return;
    }

    auto [parentPath, name] = splitPath(event.pathA);

    switch (act) {
    case ACT_NEW_FILE:
    case ACT_NEW_LINK:
    case ACT_NEW_SYMLINK:
        Q_EMIT q->fileCreated(parentPath, name);
        break;
    case ACT_NEW_FOLDER:
        Q_EMIT q->directoryCreated(parentPath, name);
        break;
    case ACT_DEL_FILE:
        Q_EMIT q->fileDeleted(parentPath, name);
        break;
    case ACT_DEL_FOLDER:
        Q_EMIT q->directoryDeleted(parentPath, name);
        break;
    case ACT_RENAME_FILE:
        Q_EMIT q->fileCreated(parentPath, name);
        break;
    case ACT_RENAME_FOLDER:
        Q_EMIT q->directoryCreated(parentPath, name);
        break;
    case ACT_CLOSE_WRITE_FILE:
        Q_EMIT q->fileClosed(parentPath, name);
        break;
    default:
        break;
    }
}

void VfsMonitorFileSystemWatcherPrivate::handleDisconnect()
{
    // The reader thread has already released the socket and the notifier.
    // Backoff: start at 1 s, double up to 30 s. Reset to 0 on a successful
    // reconnect (attemptReconnect) so the next outage starts fresh.
    if (reconnectBackoffMs <= 0)
        reconnectBackoffMs = 1000;

    if (!reconnectTimer)
        return;   // shutting down

    qInfo() << "VfsMonitor: scheduling dispatcher reconnect in" << reconnectBackoffMs << "ms";
    reconnectTimer->start(reconnectBackoffMs);
}

void VfsMonitorFileSystemWatcherPrivate::attemptReconnect()
{
    const int fd = connectDispatcherSocket();
    if (fd >= 0) {
        // Hand the new fd to the reader thread (which owns the notifier).
        pendingFd.storeRelaxed(fd);
        QMetaObject::invokeMethod(reader, [this, fd]() { reader->begin(fd); }, Qt::QueuedConnection);
        reconnectBackoffMs = 0;   // success: next outage restarts at 1 s
        qInfo() << "VfsMonitor: reconnected to deepin-anything event dispatcher";
        return;
    }

    // Grow the backoff (cap at 30 s) and retry.
    reconnectBackoffMs = std::min(reconnectBackoffMs * 2, 30000);
    if (reconnectTimer)
        reconnectTimer->start(reconnectBackoffMs);
}

// ========== VfsMonitorFileSystemWatcher ==========

VfsMonitorFileSystemWatcher::VfsMonitorFileSystemWatcher(const QStringList &rootPaths,
                                                         PathExcludePredicate excludePredicate,
                                                         QObject *parent)
    : QObject(parent), d_ptr(new VfsMonitorFileSystemWatcherPrivate(rootPaths, std::move(excludePredicate), this))
{
}

VfsMonitorFileSystemWatcher::~VfsMonitorFileSystemWatcher()
{
}

VfsMonitorFileSystemWatcher *VfsMonitorFileSystemWatcher::create(const QStringList &rootPaths,
                                                                 PathExcludePredicate excludePredicate,
                                                                 QObject *parent)
{
    auto *watcher = new VfsMonitorFileSystemWatcher(rootPaths, std::move(excludePredicate), parent);

    if (!watcher->d_func()->initDispatcher()) {
        delete watcher;
        return nullptr;
    }

    return watcher;
}

ANYTHING_INDEX_END_NAMESPACE
