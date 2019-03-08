/*
 * Copyright (C) 2017 ~ 2019 Deepin Technology Co., Ltd.
 *
 * Author:     zccrs <zccrs@live.com>
 *
 * Maintainer: zccrs <zhangjide@deepin.com>
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

class DFMBlockDevice;
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
    QStringList search(int maxCount, qint64 maxTime, quint32 startOffset, quint32 endOffset,
                       const QString &path, const QString &keyword, bool useRegExp,
                       quint32 &startOffsetReturn, quint32 &endOffsetReturn) const;

    QStringList insertFileToLFTBuf(const QByteArray &file);
    QStringList removeFileFromLFTBuf(const QByteArray &file);
    QStringList renameFileOfLFTBuf(const QByteArray &oldFile, const QByteArray &newFIle);

    void quit();

    bool autoIndexExternal() const;
    bool autoIndexInternal() const;

    int logLevel() const;

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
    bool _isAutoIndexPartition() const;

    void _syncAll();
    void _indexAll();
    void _cleanAllIndex();
    void _addPathByPartition(const DFMBlockDevice *block);
    void onMountAdded(const QString &blockDevicePath, const QByteArray &mountPoint);
    void onMountRemoved(const QString &blockDevicePath, const QByteArray &mountPoint);
    void onFSAdded(const QString &blockDevicePath);
    void onFSRemoved(const QString &blockDevicePath);
};

#endif // LFTMANAGER_H
