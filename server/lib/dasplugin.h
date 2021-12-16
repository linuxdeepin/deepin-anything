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
#ifndef DASPLUGIN_H
#define DASPLUGIN_H

#include <dasdefine.h>

#include <QObject>

DAS_BEGIN_NAMESPACE
#define DASFactoryInterface_iid "com.deepin.anything.server.DASFactoryInterface_iid"

class DASInterface;
class DASPlugin : public QObject
{
    Q_OBJECT
public:
    explicit DASPlugin(QObject *parent = nullptr);

    virtual DASInterface *create(const QString &key) = 0;
};

DAS_END_NAMESPACE

#endif // DASPLUGIN_H
