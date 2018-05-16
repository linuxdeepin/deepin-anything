/*
 * Copyright (C) 2017 ~ 2018 Deepin Technology Co., Ltd.
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
#include "dasfactory.h"
#include "daspluginloader.h"
#include "dasplugin.h"

#include <QMultiMap>

DAS_BEGIN_NAMESPACE

#ifndef QT_NO_LIBRARY
Q_GLOBAL_STATIC_WITH_ARGS(DASPluginLoader, loader,
    (DASFactoryInterface_iid, QLatin1String("/handlers"), Qt::CaseInsensitive))
#endif

QStringList DASFactory::keys()
{
    QStringList list;
#ifndef QT_NO_LIBRARY
    typedef QMultiMap<int, QString> PluginKeyMap;

    const PluginKeyMap keyMap = loader()->keyMap();
    const PluginKeyMap::const_iterator cend = keyMap.constEnd();
    for (PluginKeyMap::const_iterator it = keyMap.constBegin(); it != cend; ++it)
        list.append(it.value());
#endif
    return list;
}

DASInterface *DASFactory::create(const QString &key)
{
    return dLoadPlugin<DASInterface, DASPlugin>(loader(), key);
}

DAS_END_NAMESPACE
