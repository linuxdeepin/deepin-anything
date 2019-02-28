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

class LFTManager : public QObject
{
    Q_OBJECT

public:
    ~LFTManager();

    static LFTManager *instance();

    bool addPath(const QString &path);
    bool hasLFT(QString path) const;
    bool lftBuinding(QString path) const;

    QStringList refresh(const QByteArray &serialUriFilter = QByteArray());
    QStringList sync(const QString &mountPoint = QString());

    QStringList search(const QString &path, const QString keyword, bool useRegExp = false) const;

Q_SIGNALS:
    void addPathFinished(const QString &path, bool success);

protected:
    explicit LFTManager(QObject *parent = nullptr);

private:
    void onMountAdded(const QString &blockDevicePath, const QByteArray &mountPoint);
    void onMountRemoved(const QString &blockDevicePath, const QByteArray &mountPoint);
};

#endif // LFTMANAGER_H
