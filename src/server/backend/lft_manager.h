#ifndef ANYTHING_LFT_MANAGER_H
#define ANYTHING_LFT_MANAGER_H

#include <QDBusContext>
#include <QObject>
#include <QSettings>
#include <QStringList>

// Forward declaration
class DBlockDevice;
class QFutureWatcherBase;
extern "C" {
    struct __fs_buf__;
    typedef struct __fs_buf__ fs_buf;
}

namespace anything {

class lft_manager : public QObject, protected QDBusContext {
    Q_OBJECT

public:
    lft_manager(QObject* parent = nullptr);
    ~lft_manager();

    bool add_path(QString path, bool auto_index = false);

protected:


private:
    void add_path_by_partition(const DBlockDevice* block);
    bool check_authorization();
    QString get_root_mount_point(const DBlockDevice* block);
    void index_all(bool force);
    bool lft_exists(const QString& path);
    QPair<QString, fs_buf*> retrieve_fsbuf(const QString& path);

    static fs_buf* build_fsbuf(QFutureWatcherBase* futureWatcher, const QString& path);
    static QString cache_directory();
    static bool check_fsbuf(fs_buf* buf);
    static int handle_build_fs_buf_progress(uint32_t file_count, uint32_t dir_count, const char* cur_dir, const char* cur_file, void* param);

private:
    QSettings settings_;
    QStringList build_paths_;
    QMap<QString, fs_buf*> fsbuf_;
};

} // namespace anything

#endif // ANYTHING_LFT_MANAGER_H