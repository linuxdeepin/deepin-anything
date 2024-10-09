#ifndef ANYTHING_PARTITION_H
#define ANYTHING_PARTITION_H

#include <QMap>
#include <QByteArray>


namespace anything {

#define MKDEV(ma,mi)    ((ma)<<8 | (mi))

class partition {
public:
    bool update();

    template<class T>
    auto contains(const T& key) const {
        return partitions.contains(key);
    }

    template<class T>
    auto operator[](const T& key) const {
        return partitions[key].toStdString();
    }

    bool path_match_type(std::string path, std::string type);

private:
    bool write_vfs_unnamed_device(const char* str);
    bool read_vfs_unnamed_device(QSet<QByteArray>& devices);
    bool update_vfs_unnamed_device(const QSet<QByteArray>& news);

private:
    QMap<unsigned int, QByteArray> partitions;
};

} // namespace anything

#endif // ANYTHING_PARTITION_H