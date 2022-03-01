/*
 * Copyright (C) 2021 UOS Technology Co., Ltd.
 *
 * Author:     zccrs <zccrs@live.com>
 *
 * Maintainer: zccrs <zhangjide@deepin.com>
 *             yangwu <yangwu@uniontech.com>
 *             wangrong <wangrong@uniontech.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef LFTMANAGER_H
#define LFTMANAGER_H

#include <QObject>
#include <QDBusContext>
#include <QTimer>

class DBlockDevice;
class LFTManager : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_PROPERTY(bool autoIndexInternal READ autoIndexInternal WRITE setAutoIndexInternal NOTIFY autoIndexInternalChanged)
    Q_PROPERTY(bool autoIndexExternal READ autoIndexExternal WRITE setAutoIndexExternal NOTIFY autoIndexExternalChanged)
    Q_PROPERTY(int logLevel READ logLevel WRITE setLogLevel)

public:
    ~LFTManager();

    static LFTManager *instance();
    static QString cacheDir();
    static QStringList logCategoryList();
    static QByteArray setCodecNameForLocale(const QByteArray &codecName);

    bool addPath(QString path, bool autoIndex = false);
    bool removePath(const QString &path);
    bool hasLFT(const QString &path) const;
    bool lftBuinding(const QString &path) const;
    bool cancelBuild(const QString &path);

    QStringList allPath() const;
    QStringList hasLFTSubdirectories(QString path) const;

    QStringList refresh(const QByteArray &serialUriFilter = QByteArray());
    QStringList sync(const QString &mountPoint = QString());

    QStringList search(const QString &path, const QString &keyword, bool useRegExp = false) const;
    QStringList search(int maxCount, qint64 icase, quint32 startOffset, quint32 endOffset,
                       const QString &path, const QString &keyword, bool useRegExp,
                       quint32 &startOffsetReturn, quint32 &endOffsetReturn) const;

    QStringList insertFileToLFTBuf(const QByteArray &file);
    QStringList removeFileFromLFTBuf(const QByteArray &file);
    QStringList renameFileOfLFTBuf(const QByteArray &oldFile, const QByteArray &newFIle);

    void quit();

    bool autoIndexExternal() const;
    bool autoIndexInternal() const;

    int logLevel() const;
    QStringList parallelsearch(const QString &path, const QString &keyword, const QStringList &rules) const;
    QStringList parallelsearch(const QString &path, quint32 startOffset, quint32 endOffset,
                               const QString &keyword, const QStringList &rules,
                               quint32 &startOffsetReturn, quint32 &endOffsetReturn) const;

public Q_SLOTS:
    void setAutoIndexExternal(bool autoIndexExternal);
    void setAutoIndexInternal(bool autoIndexInternal);

    void setLogLevel(int logLevel);

Q_SIGNALS:
    void addPathFinished(const QString &path, bool success);
    void autoIndexExternalChanged(bool autoIndexExternal);
    void autoIndexInternalChanged(bool autoIndexInternal);

protected:
    explicit LFTManager(QObject *parent = nullptr);

    void sendErrorReply(QDBusError::ErrorType type, const QString &msg = QString()) const;

private:
    QTimer refresh_timer;
    bool _isAutoIndexPartition() const;

    void _syncAll();
    void _indexAll();
    void _indexAllDelay(int time = 10 * 60 * 1000);
    void _cleanAllIndex();
    void _addPathByPartition(const DBlockDevice *block);
    void onMountAdded(const QString &blockDevicePath, const QByteArray &mountPoint);
    void onMountRemoved(const QString &blockDevicePath, const QByteArray &mountPoint);
    void onFSAdded(const QString &blockDevicePath);
    void onFSRemoved(const QString &blockDevicePath);

    // search related private implementations.
    int _prepareBuf(quint32 *startOffset, quint32 *endOffset, const QString &path, void **buf, QString *newpath) const;
    int _separateSearchArgs(const QStringList &rules, bool *useRegExp, quint32 *startOffset, quint32 *endOffset, qint64 *maxTime, qint64 *maxCount) const;
    bool _getRuleArgs(const QStringList &rules, int searchFlag, quint32 &valueReturn) const;
    bool _parseRules(void **prules, const QStringList &rules) const;
    QStringList _setRulesByDefault(const QStringList &rules, quint32 startOffset, quint32 endOffset) const;
    QStringList _enterSearch(const QString &path, const QString &keyword, const QStringList &rules, quint32 &startOffsetReturn, quint32 &endOffsetReturn) const;
    int _doSearch(void *vbuf, quint32 maxCount, const QString &keyword, quint32 *startOffset, quint32 *endOffset, QList<uint32_t> &results, const QStringList &rules = {}) const;
};

#endif // LFTMANAGER_H
