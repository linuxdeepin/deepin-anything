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
#ifndef DASINTERFACE_H
#define DASINTERFACE_H

#include <dasdefine.h>

#include <QObject>

DAS_BEGIN_NAMESPACE

class DASInterface : public QObject
{
    Q_OBJECT
public:
    explicit DASInterface(QObject *parent = nullptr);

public Q_SLOTS:
    virtual void onFileCreate(const QByteArrayList &files) = 0;
    virtual void onFileDelete(const QByteArrayList &files) = 0;
    virtual void onFileRename(const QList<QPair<QByteArray, QByteArray>> &files) = 0;
};

DAS_END_NAMESPACE

#endif // DASINTERFACE_H
