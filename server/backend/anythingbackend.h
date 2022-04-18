/*
 * Copyright (C) 2020 ~ 2021 Uniontech Software Technology Co., Ltd.
 *
 * Author:     yanghao<yanghao@uniontech.com>
 *
 * Maintainer: zhengyouge<zhengyouge@uniontech.com>
 *             yanghao<yanghao@uniontech.com>
 *             hujianzhong<hujianzhong@uniontech.com>
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

#ifndef ANYTHINGBACKEND_H
#define ANYTHINGBACKEND_H

#include <dasdefine.h>
#include <QMap>
#include <QList>
#include <QObject>

DAS_BEGIN_NAMESPACE

class Server;
class AnythingBackend : public QObject
{
    Q_OBJECT
public:
    explicit AnythingBackend(QObject *parent = nullptr);
    ~AnythingBackend();

    int monitorStart();
    int backendRun();

protected:


private:
    void init_connection()noexcept;
    void addPlugin(const QString &key, Server *server);
    void removePlugins(const QStringList &keys, Server *server);
    int writeMountInfo();
};

DAS_END_NAMESPACE
#endif // ANYTHINGBACKEND_H
