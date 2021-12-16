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
#ifndef SERVER_H
#define SERVER_H

#include "dasdefine.h"

#include <QThread>

DAS_BEGIN_NAMESPACE

class Server : public QThread
{
    Q_OBJECT

public:
    explicit Server(QObject *parent = nullptr);

signals:
    void fileCreated(QByteArrayList files);
    void fileDeleted(QByteArrayList files);
    void fileRenamed(QList<QPair<QByteArray, QByteArray>> files);

private:
    void run() override;
};

DAS_END_NAMESPACE

#endif // SERVER_H
