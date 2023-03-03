// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daspluginloader.h"

#include <QMutex>
#include <QDir>
#include <QJsonArray>
#include <QPluginLoader>
#include <QCoreApplication>
#include <QFileSystemWatcher>
#include <QDateTime>
#include <QDebug>
#include <QLoggingCategory>

DAS_BEGIN_NAMESPACE

Q_GLOBAL_STATIC(QList<DASPluginLoader *>, qt_factory_loaders)

Q_GLOBAL_STATIC_WITH_ARGS(QMutex, qt_factoryloader_mutex, (QMutex::Recursive))
Q_GLOBAL_STATIC(QFileSystemWatcher, fileWatcher)

Q_LOGGING_CATEGORY(lcLoader, "anything.monitor.pluginloader", DEFAULT_MSG_TYPE)
#define loaderDebug(...) qCDebug(lcLoader, __VA_ARGS__)

// avoid duplicate QStringLiteral data:
inline QString iidKeyLiteral() { return QStringLiteral("IID"); }
#ifdef QT_SHARED
inline QString versionKeyLiteral() { return QStringLiteral("version"); }
#endif
inline QString metaDataKeyLiteral() { return QStringLiteral("MetaData"); }
inline QString keysKeyLiteral() { return QStringLiteral("Keys"); }

/* Internal, for debugging */
static bool das_debug_component()
{
#ifdef QT_DEBUG
    return true;
#endif

    static int debug_env = QT_PREPEND_NAMESPACE(qEnvironmentVariableIntValue)("DAS_DEBUG_PLUGINS");
    return debug_env != 0;
}

class DASPluginLoaderPrivate
{
    Q_DECLARE_PUBLIC(DASPluginLoader)
public:
    DASPluginLoaderPrivate(DASPluginLoader *qq);
    ~DASPluginLoaderPrivate();
    mutable QMutex mutex;
    QByteArray iid;
    QList<QPluginLoader*> pluginLoaderList;
    QMap<QString, uint> pluginFileLastModified;
    QMultiMap<QString,QPluginLoader*> keyMap;
    QString suffix;
    Qt::CaseSensitivity cs;
    bool rki;
    QStringList loadedPaths;
    QStringList watchedPaths;

    DASPluginLoader *q_ptr;

    static QStringList pluginPaths;

    QStringList getKeys(const QPluginLoader *loader, bool *metaDataOk = 0) const;
    QPluginLoader *loadPlugin(const QString &fileName);
    void _q_onDirectoryChanged(const QString &path);
};

QStringList DASPluginLoaderPrivate::pluginPaths;

DASPluginLoaderPrivate::DASPluginLoaderPrivate(DASPluginLoader *qq)
    : q_ptr(qq)
{
    if (pluginPaths.isEmpty()) {
        if (QT_PREPEND_NAMESPACE(qEnvironmentVariableIsEmpty)("DAS_PLUGIN_PATH"))
            pluginPaths.append(QString::fromLocal8Bit(PLUGINDIR).split(':'));
        else
            pluginPaths = QString::fromLocal8Bit(qgetenv("DAS_PLUGIN_PATH")).split(':');
    }

    if (das_debug_component())
        loaderDebug() << "plugin paths:" << pluginPaths;
}

DASPluginLoaderPrivate::~DASPluginLoaderPrivate()
{
    for (int i = 0; i < pluginLoaderList.count(); ++i) {
        QPluginLoader *loader = pluginLoaderList.at(i);
        loader->unload();
    }

    for (const QString &path : watchedPaths) {
        fileWatcher->removePath(path);
    }
}

QStringList DASPluginLoaderPrivate::getKeys(const QPluginLoader *loader, bool *metaDataOk) const
{
    QStringList keys;
    QString iid = loader->metaData().value(iidKeyLiteral()).toString();

    if (metaDataOk)
        *metaDataOk = false;

    if (iid == QLatin1String(this->iid.constData(), this->iid.size())) {
        QJsonObject object = loader->metaData().value(metaDataKeyLiteral()).toObject();

        if (metaDataOk)
            *metaDataOk = true;

        QJsonArray k = object.value(keysKeyLiteral()).toArray();
        for (int i = 0; i < k.size(); ++i)
            keys += cs ? k.at(i).toString() : k.at(i).toString().toLower();
    }

    return keys;
}

QPluginLoader *DASPluginLoaderPrivate::loadPlugin(const QString &fileName)
{
    QPluginLoader *loader = 0;

#ifdef Q_OS_MAC
    if (isLoadingDebugAndReleaseCocoa) {
#ifdef QT_DEBUG
        if (fileName.contains(QStringLiteral("libqcocoa.dylib")))
            return 0;    // Skip release plugin in debug mode
#else
        if (fileName.contains(QStringLiteral("libqcocoa_debug.dylib")))
            return 0;    // Skip debug plugin in release mode
#endif
    }
#endif
    if (das_debug_component()) {
        loaderDebug() << "PluginLoader::PluginLoader() looking at" << fileName;
    }
    loader = (new QPluginLoader(fileName, q_ptr));
    loader->setLoadHints(QLibrary::ResolveAllSymbolsHint);

    if (!loader->load()) {
        if (das_debug_component()) {
            loaderDebug() << loader->errorString();
        }
        loader->deleteLater();
        return 0;
    }

    bool metaDataOk = false;
    QStringList keys = getKeys(loader, &metaDataOk);

    if (das_debug_component())
        loaderDebug() << "Got keys from plugin meta data" << keys;

    if (!metaDataOk) {
        if (das_debug_component()) {
            loaderDebug() << "failed on load meta data";
        }

        loader->deleteLater();
        return 0;
    }

    int keyUsageCount = 0;
    for (int k = 0; k < keys.count(); ++k) {
        // first come first serve, unless the first
        // library was built with a future Qt version,
        // whereas the new one has a Qt version that fits
        // better
        const QString &key = keys.at(k);

        if (rki) {
            keyMap.insertMulti(key, loader);
            ++keyUsageCount;
        } else {
            QPluginLoader *previous = keyMap.value(key);
            int prev_dfm_version = 0;
            if (previous) {
                prev_dfm_version = (int)previous->metaData().value(versionKeyLiteral()).toDouble();
            }
            int dfm_version = (int)loader->metaData().value(versionKeyLiteral()).toDouble();
            if (!previous || (prev_dfm_version > QString(QMAKE_VERSION).toDouble() && dfm_version <= QString(QMAKE_VERSION).toDouble())) {
                keyMap.insertMulti(key, loader);
                ++keyUsageCount;
            }
        }
    }
    if (keyUsageCount || keys.isEmpty()) {
        pluginLoaderList += loader;

        QFileInfo info(fileName);

        pluginFileLastModified[info.absoluteFilePath()] = info.lastModified().toTime_t();

        if (das_debug_component()) {
            loaderDebug() << "file last modified:" << info.lastModified();
        }
    } else {
        if (das_debug_component()) {
            loaderDebug() << "the plugin all key is occupied";
        }

        loader->deleteLater();
        loader = 0;
    }

    return loader;
}

void DASPluginLoaderPrivate::_q_onDirectoryChanged(const QString &path)
{
    if (das_debug_component()) {
        loaderDebug() << "directory changed:" << path;
    }

    if (!watchedPaths.contains(path)) {
        return;
    }

    const QStringList &plugins = QDir(path).entryList(QDir::Files);
    QStringList old_file_list = pluginFileLastModified.keys();
    QStringList new_file_list;
    QStringList modified_file_list;

    if (das_debug_component()) {
        loaderDebug() << "old files:" << old_file_list;
        loaderDebug() << "existing files:" << plugins;
    }

    for (int j = 0; j < plugins.count(); ++j) {
        QFileInfo info(path + QLatin1Char('/') + plugins.at(j));
        const QString &fileName = info.absoluteFilePath();

        if (pluginFileLastModified.contains(fileName)) {
            old_file_list.removeOne(fileName);

            uint last_modified = pluginFileLastModified.value(fileName);

            if (last_modified != info.lastModified().toTime_t()) {
                modified_file_list << fileName;
            }

            loaderDebug() << "modified date time, old:" << QDateTime::fromTime_t(last_modified) << "new:" << info.lastModified();
        } else {
            new_file_list << fileName;
        }
    }

    if (das_debug_component()) {
        loaderDebug() << "dirty files:" << old_file_list;
        loaderDebug() << "new files:" << new_file_list;
        loaderDebug() << "modified files:" << modified_file_list;
    }

    QList<QPluginLoader*> dirtyLoaders;
    QList<QPluginLoader*> needUpdateLoaders;

    for (int i = 0; i < pluginLoaderList.count(); ++i) {
        QPluginLoader *l = pluginLoaderList.at(i);

        if (old_file_list.contains(l->fileName())) {
            dirtyLoaders.append(l);
        }

        if (modified_file_list.contains(l->fileName())) {
            needUpdateLoaders.append(l);
        }
    }

    for (QPluginLoader *l : dirtyLoaders) {
        const QStringList &keys = getKeys(l);

        loaderDebug() << "plugin deleted, keyes:" << keys << "in file:" << l->fileName();

        emit q_ptr->pluginRemoved(l, keys);
    }

    for (const QString &file : new_file_list) {
        if (QPluginLoader *l = loadPlugin(file)) {
            for (const QString &key : getKeys(l)) {
                if (das_debug_component())
                    loaderDebug() << "add plugin, key:" << key << "in file:" << file;

                emit q_ptr->pluginAdded(key);
            }
        }
    }

    for (QPluginLoader *l : needUpdateLoaders) {
        const QStringList &keys = getKeys(l);

        loaderDebug() << "plugin modified, keyes:" << keys << "in file:" << l->fileName();

        emit q_ptr->pluginModified(l, keys);
    }
}

DASPluginLoader::DASPluginLoader(const char *iid,
                                 const QString &suffix,
                                 Qt::CaseSensitivity cs,
                                 bool repetitiveKeyInsensitive)
    : QObject()
    , d_ptr(new DASPluginLoaderPrivate(this))
{
    Q_D(DASPluginLoader);
    d->iid = iid;
    d->suffix = suffix;
    d->cs = cs;
    d->rki = repetitiveKeyInsensitive;

    connect(fileWatcher, SIGNAL(directoryChanged(const QString &)), this, SLOT(_q_onDirectoryChanged(const QString &)));

    for (int i = 0; i < d->pluginPaths.count(); ++i) {
        d->pluginPaths[i] = QDir(d->pluginPaths[i]).absolutePath();
        const QString &path = QDir::cleanPath(d->pluginPaths.at(i) + suffix);

        if (QFile::exists(path)) {
            if (fileWatcher->addPath(path)) {
                d->watchedPaths << path;

                if (das_debug_component())
                    loaderDebug() << "watch:" << path;
            } else if (das_debug_component()) {
                loaderDebug() << "failed on add watch:" << path;
            }
        }
    }

    QMutexLocker locker(qt_factoryloader_mutex());
    Q_UNUSED(locker)
    update();
    qt_factory_loaders()->append(this);
}

void DASPluginLoader::update()
{
#ifdef QT_SHARED
    Q_D(DASPluginLoader);

    const QStringList &paths = d->pluginPaths;
    for (int i = 0; i < paths.count(); ++i) {
        const QString &pluginDir = paths.at(i);
        // Already loaded, skip it...
        if (d->loadedPaths.contains(pluginDir))
            continue;
        d->loadedPaths << pluginDir;

        QString path = pluginDir + d->suffix;

        if (das_debug_component())
            loaderDebug() << "PluginLoader::PluginLoader() checking directory path" << path << "...";

        if (!QDir(path).exists(QLatin1String(".")))
            continue;

        QStringList plugins = QDir(path).entryList(QDir::Files);

#ifdef Q_OS_MAC
        // Loading both the debug and release version of the cocoa plugins causes the objective-c runtime
        // to print "duplicate class definitions" warnings. Detect if DFMFactoryLoader is about to load both,
        // skip one of them (below).
        //
        // ### FIXME find a proper solution
        //
        const bool isLoadingDebugAndReleaseCocoa = plugins.contains(QStringLiteral("libqcocoa_debug.dylib"))
                && plugins.contains(QStringLiteral("libqcocoa.dylib"));
#endif
        for (int j = 0; j < plugins.count(); ++j) {
            QString fileName = QDir::cleanPath(path + QLatin1Char('/') + plugins.at(j));

            d->loadPlugin(QFileInfo(fileName).absoluteFilePath());
        }
    }
#else
    Q_D(PluginLoader);
    if (dfm_debug_component()) {
        loaderDebug() << "PluginLoader::PluginLoader() ignoring" << d->iid
                 << "since plugins are disabled in static builds";
    }
#endif
}

bool DASPluginLoader::removeLoader(QPluginLoader *loader)
{
    Q_D(DASPluginLoader);

    if (!loader->unload()) {
        if (das_debug_component())
            loaderDebug() << loader->errorString();

        return false;
    }

    d->pluginLoaderList.removeOne(loader);
    d->pluginFileLastModified.remove(loader->fileName());

    for (const QString &key : d->getKeys(loader)) {
        d->keyMap.remove(key, loader);
    }

    if (das_debug_component()) {
        loaderDebug() << "plugin is removed:" << loader->fileName();
    }

    loader->deleteLater();

    return true;
}

QPluginLoader *DASPluginLoader::reloadLoader(QPluginLoader *loader)
{
    Q_D(DASPluginLoader);

    const QString &file_name = loader->fileName();

    if (removeLoader(loader)) {
        loader = d->loadPlugin(file_name);
    } else {
        loader = nullptr;
    }

    if (das_debug_component()) {
        if (loader)
            loaderDebug() << "plugin is reload:" << file_name;
        else
            loaderDebug() << "failed on reload loader, file name:" << file_name;
    }

    return loader;
}

DASPluginLoader::~DASPluginLoader()
{
    QMutexLocker locker(qt_factoryloader_mutex());
    qt_factory_loaders()->removeAll(this);
}

QStringList DASPluginLoader::logCategoryList()
{
    QStringList list;

    list << lcLoader().categoryName();

    return list;
}

QList<QJsonObject> DASPluginLoader::metaData() const
{
    Q_D(const DASPluginLoader);
    QMutexLocker locker(&d->mutex);
    QList<QJsonObject> metaData;
    for (int i = 0; i < d->pluginLoaderList.size(); ++i)
        metaData.append(d->pluginLoaderList.at(i)->metaData());

    return metaData;
}

QObject *DASPluginLoader::instance(int index) const
{
    Q_D(const DASPluginLoader);
    if (index < 0)
        return 0;

    if (index < d->pluginLoaderList.size()) {
        QPluginLoader *loader = d->pluginLoaderList.at(index);
        if (loader->instance()) {
            QObject *obj = loader->instance();
            if (obj) {
                if (!obj->parent())
                    obj->moveToThread(qApp->thread());
                return obj;
            }
        }
        return 0;
    }

    return 0;
}

#if defined(Q_OS_UNIX) && !defined (Q_OS_MAC)
QPluginLoader *DASPluginLoader::pluginLoader(const QString &key) const
{
    Q_D(const DASPluginLoader);
    return d->keyMap.value(d->cs ? key : key.toLower());
}

QList<QPluginLoader*> DASPluginLoader::pluginLoaderList(const QString &key) const
{
    Q_D(const DASPluginLoader);
    return d->keyMap.values(d->cs ? key : key.toLower());
}
#endif

void DASPluginLoader::refreshAll()
{
    QMutexLocker locker(qt_factoryloader_mutex());
    QList<DASPluginLoader *> *loaders = qt_factory_loaders();
    for (QList<DASPluginLoader *>::const_iterator it = loaders->constBegin();
         it != loaders->constEnd(); ++it) {
        (*it)->update();
    }
}

QMultiMap<int, QString> DASPluginLoader::keyMap() const
{
    QMultiMap<int, QString> result;
    const QString metaDataKey = metaDataKeyLiteral();
    const QString keysKey = keysKeyLiteral();
    const QList<QJsonObject> metaDataList = metaData();
    for (int i = 0; i < metaDataList.size(); ++i) {
        const QJsonObject metaData = metaDataList.at(i).value(metaDataKey).toObject();
        const QJsonArray keys = metaData.value(keysKey).toArray();
        const int keyCount = keys.size();
        for (int k = 0; k < keyCount; ++k)
            result.insert(i, keys.at(k).toString());
    }
    return result;
}

int DASPluginLoader::indexOf(const QString &needle) const
{
    const QString metaDataKey = metaDataKeyLiteral();
    const QString keysKey = keysKeyLiteral();
    const QList<QJsonObject> metaDataList = metaData();
    for (int i = 0; i < metaDataList.size(); ++i) {
        const QJsonObject metaData = metaDataList.at(i).value(metaDataKey).toObject();
        const QJsonArray keys = metaData.value(keysKey).toArray();
        const int keyCount = keys.size();
        for (int k = 0; k < keyCount; ++k) {
            if (!keys.at(k).toString().compare(needle, Qt::CaseInsensitive))
                return i;
        }
    }
    return -1;
}

QList<int> DASPluginLoader::getAllIndexByKey(const QString &needle) const
{
    QList<int> list;

    const QString metaDataKey = metaDataKeyLiteral();
    const QString keysKey = keysKeyLiteral();
    const QList<QJsonObject> metaDataList = metaData();
    for (int i = 0; i < metaDataList.size(); ++i) {
        const QJsonObject metaData = metaDataList.at(i).value(metaDataKey).toObject();
        const QJsonArray keys = metaData.value(keysKey).toArray();
        const int keyCount = keys.size();
        for (int k = 0; k < keyCount; ++k) {
            if (!keys.at(k).toString().compare(needle, Qt::CaseInsensitive))
                list << i;
        }
    }
    return list;
}

QStringList DASPluginLoader::getKeysByLoader(const QPluginLoader *loader, bool *metaDataOk) const
{
    Q_D(const DASPluginLoader);

    return d->getKeys(loader, metaDataOk);
}

DAS_END_NAMESPACE

#include "moc_daspluginloader.cpp"
