// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PLUGINLOADER_H
#define PLUGINLOADER_H

#include <dasdefine.h>

#include <QPluginLoader>

DAS_BEGIN_NAMESPACE

class DASPluginLoaderPrivate;
class DASPluginLoader : public QObject
{
    Q_OBJECT
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
    QStringList getKeysByLoader(const QPluginLoader *loader, bool *metaDataOk = nullptr) const;

    void update();
    bool removeLoader(QPluginLoader *loader);
    // reload plugin, return loader list if finished
    QPluginLoader *reloadLoader(QPluginLoader *loader);

    static void refreshAll();

Q_SIGNALS:
    void pluginAdded(const QString &key);
    void pluginRemoved(QPluginLoader *loader, const QStringList &keys);
    void pluginModified(QPluginLoader *loader, const QStringList &keys);

private:
    QScopedPointer<DASPluginLoaderPrivate> d_ptr;

    Q_PRIVATE_SLOT(d_ptr, void _q_onDirectoryChanged(const QString &pah))
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
