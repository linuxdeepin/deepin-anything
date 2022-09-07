// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dasfactory.h"
#include "daspluginloader.h"
#include "dasplugin.h"

#include <QMultiMap>

DAS_BEGIN_NAMESPACE

#ifndef QT_NO_LIBRARY
Q_GLOBAL_STATIC_WITH_ARGS(DASPluginLoader, _loader,
    (DASFactoryInterface_iid, QLatin1String("/handlers"), Qt::CaseInsensitive))
#endif

QStringList DASFactory::keys()
{
    QStringList list;
#ifndef QT_NO_LIBRARY
    typedef QMultiMap<int, QString> PluginKeyMap;

    const PluginKeyMap keyMap = _loader()->keyMap();
    const PluginKeyMap::const_iterator cend = keyMap.constEnd();
    for (PluginKeyMap::const_iterator it = keyMap.constBegin(); it != cend; ++it)
        list.append(it.value());
#endif
    return list;
}

DASInterface *DASFactory::create(const QString &key)
{
    return dLoadPlugin<DASInterface, DASPlugin>(_loader(), key);
}

DASPluginLoader *DASFactory::loader()
{
    return _loader;
}

DAS_END_NAMESPACE
