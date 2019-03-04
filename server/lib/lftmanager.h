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

class LFTManager : public QObject, protected QDBusContext
{
    Q_OBJECT

public:
    ~LFTManager();

    static LFTManager *instance();

    bool addPath(QString path);
    bool hasLFT(const QString &path) const;
    bool lftBuinding(const QString &path) const;

    QStringList allPath() const;
    QStringList hasLFTSubdirectories(QString path) const;

    QStringList refresh(const QByteArray &serialUriFilter = QByteArray());
    QStringList sync(const QString &mountPoint = QString());

    QStringList search(const QString &path, const QString keyword, bool useRegExp = false) const;

    void insertFileToLFTBuf(const QString &file);
    void removeFileFromLFTBuf(const QString &file);
    void renameFileOfLFTBuf(const QString &oldFile, const QString &newFIle);

Q_SIGNALS:
    void addPathFinished(const QString &path, bool success);

protected:
    explicit LFTManager(QObject *parent = nullptr);

private:
    void onMountAdded(const QString &blockDevicePath, const QByteArray &mountPoint);
    void onMountRemoved(const QString &blockDevicePath, const QByteArray &mountPoint);
};

#endif // LFTMANAGER_H
