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
#ifndef PLUGINLOADER_H
#define PLUGINLOADER_H

#include <dasdefine.h>

#include <QPluginLoader>

DAS_BEGIN_NAMESPACE

class DASPluginLoaderPrivate;
class DASPluginLoader
{
    Q_DECLARE_PRIVATE(DASPluginLoader)
public:
    explicit DASPluginLoader(const char *iid,
                          const QString &suffix = QString(),
                          Qt::CaseSensitivity = Qt::CaseSensitive,
                          bool repetitiveKeyInsensitive  = false);
    ~DASPluginLoader();

    QList<QJsonObject> metaData() const;
    QObject *instance(int index) const;

#if defined(Q_OS_UNIX) && !defined (Q_OS_MAC)
    QPluginLoader *pluginLoader(const QString &key) const;
    QList<QPluginLoader*> pluginLoaderList(const QString &key) const;
#endif

    QMultiMap<int, QString> keyMap() const;
    int indexOf(const QString &needle) const;
    QList<int> getAllIndexByKey(const QString &needle) const;

    void update();

    static void refreshAll();

private:
    QScopedPointer<DASPluginLoaderPrivate> d_ptr;
};

template <class PluginInterface, class FactoryInterface>
    PluginInterface *dLoadPlugin(const DASPluginLoader *loader, const QString &key)
{
    const int index = loader->indexOf(key);
    if (index != -1) {
        QObject *factoryObject = loader->instance(index);
        if (FactoryInterface *factory = qobject_cast<FactoryInterface *>(factoryObject))
            if (PluginInterface *result = factory->create(key))
                return result;
    }
    return 0;
}

template <class PluginInterface, class FactoryInterface>
    QList<PluginInterface*> dLoadPluginList(const DASPluginLoader *loader, const QString &key)
{
    QList<PluginInterface*> list;

    for (int index : loader->getAllIndexByKey(key)) {
        if (index != -1) {
            QObject *factoryObject = loader->instance(index);
            if (FactoryInterface *factory = qobject_cast<FactoryInterface *>(factoryObject))
                if (PluginInterface *result = factory->create(key))
                    list << result;
        }
    }

    return list;
}

template <class PluginInterface, class FactoryInterface, class Parameter1>
PluginInterface *dLoadPlugin(const DASPluginLoader *loader,
                              const QString &key,
                              const Parameter1 &parameter1)
{
    const int index = loader->indexOf(key);
    if (index != -1) {
        QObject *factoryObject = loader->instance(index);
        if (FactoryInterface *factory = qobject_cast<FactoryInterface *>(factoryObject))
            if (PluginInterface *result = factory->create(key, parameter1))
                return result;
    }
    return 0;
}

DAS_END_NAMESPACE

#endif // PLUGINLOADER_H
