// SPDX-FileCopyrightText: 2024 - 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AnythingAdaptor.h"
#include <QtCore/QMetaObject>
#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>

/*
 * Implementation of adaptor class AnythingAdaptor
 */

AnythingAdaptor::AnythingAdaptor(QObject *parent)
    : QDBusAbstractAdaptor(parent)
{
    // constructor
    setAutoRelaySignals(true);
}

AnythingAdaptor::~AnythingAdaptor()
{
    // destructor
}

bool AnythingAdaptor::autoIndexExternal() const
{
    // get the value of property autoIndexExternal
    return qvariant_cast< bool >(parent()->property("autoIndexExternal"));
}

void AnythingAdaptor::setAutoIndexExternal(bool value)
{
    // set the value of property autoIndexExternal
    parent()->setProperty("autoIndexExternal", QVariant::fromValue(value));
}

bool AnythingAdaptor::autoIndexInternal() const
{
    // get the value of property autoIndexInternal
    return qvariant_cast< bool >(parent()->property("autoIndexInternal"));
}

void AnythingAdaptor::setAutoIndexInternal(bool value)
{
    // set the value of property autoIndexInternal
    parent()->setProperty("autoIndexInternal", QVariant::fromValue(value));
}

int AnythingAdaptor::logLevel() const
{
    // get the value of property logLevel
    return qvariant_cast< int >(parent()->property("logLevel"));
}

void AnythingAdaptor::setLogLevel(int value)
{
    // set the value of property logLevel
    parent()->setProperty("logLevel", QVariant::fromValue(value));
}

bool AnythingAdaptor::addPath(const QString &path)
{
    // handle method call com.deepin.anything.addPath
    bool success;
    QMetaObject::invokeMethod(parent(), "addPath", Q_RETURN_ARG(bool, success), Q_ARG(QString, path));
    return success;
}

QStringList AnythingAdaptor::allPath()
{
    // handle method call com.deepin.anything.allPath
    QStringList pathList;
    QMetaObject::invokeMethod(parent(), "allPath", Q_RETURN_ARG(QStringList, pathList));
    return pathList;
}

QString AnythingAdaptor::cacheDir()
{
    // handle method call com.deepin.anything.cacheDir
    QString path;
    QMetaObject::invokeMethod(parent(), "cacheDir", Q_RETURN_ARG(QString, path));
    return path;
}

bool AnythingAdaptor::cancelBuild(const QString &path)
{
    // handle method call com.deepin.anything.cancelBuild
    bool success;
    QMetaObject::invokeMethod(parent(), "cancelBuild", Q_RETURN_ARG(bool, success), Q_ARG(QString, path));
    return success;
}

bool AnythingAdaptor::hasLFT(const QString &path)
{
    // handle method call com.deepin.anything.hasLFT
    bool success;
    QMetaObject::invokeMethod(parent(), "hasLFT", Q_RETURN_ARG(bool, success), Q_ARG(QString, path));
    return success;
}

QStringList AnythingAdaptor::hasLFTSubdirectories(const QString &path)
{
    // handle method call com.deepin.anything.hasLFTSubdirectories
    QStringList directories;
    QMetaObject::invokeMethod(parent(), "hasLFTSubdirectories", Q_RETURN_ARG(QStringList, directories), Q_ARG(QString, path));
    return directories;
}

QStringList AnythingAdaptor::insertFileToLFTBuf(const QByteArray &filePath)
{
    // handle method call com.deepin.anything.insertFileToLFTBuf
    QStringList bufRootPathList;
    QMetaObject::invokeMethod(parent(), "insertFileToLFTBuf", Q_RETURN_ARG(QStringList, bufRootPathList), Q_ARG(QByteArray, filePath));
    return bufRootPathList;
}

bool AnythingAdaptor::lftBuinding(const QString &path)
{
    // handle method call com.deepin.anything.lftBuinding
    bool success;
    QMetaObject::invokeMethod(parent(), "lftBuinding", Q_RETURN_ARG(bool, success), Q_ARG(QString, path));
    return success;
}

QStringList AnythingAdaptor::parallelsearch(const QString &path, uint startOffset, uint endOffset, const QString &keyword, const QStringList &rules, uint &startOffset_, uint &endOffset_)
{
    // handle method call com.deepin.anything.parallelsearch
    //return static_cast<YourObjectType *>(parent())->parallelsearch(path, startOffset, endOffset, keyword, rules, startOffset_, endOffset_);
}

QStringList AnythingAdaptor::parallelsearch(const QString &path, const QString &keyword, const QStringList &rules)
{
    // handle method call com.deepin.anything.parallelsearch
    QStringList results;
    QMetaObject::invokeMethod(parent(), "parallelsearch", Q_RETURN_ARG(QStringList, results), Q_ARG(QString, path), Q_ARG(QString, keyword), Q_ARG(QStringList, rules));
    return results;
}

void AnythingAdaptor::quit()
{
    // handle method call com.deepin.anything.quit
    QMetaObject::invokeMethod(parent(), "quit");
}

QStringList AnythingAdaptor::refresh(const QByteArray &serialUriFilter)
{
    // handle method call com.deepin.anything.refresh
    QStringList rootPathList;
    QMetaObject::invokeMethod(parent(), "refresh", Q_RETURN_ARG(QStringList, rootPathList), Q_ARG(QByteArray, serialUriFilter));
    return rootPathList;
}

QStringList AnythingAdaptor::removeFileFromLFTBuf(const QByteArray &filePath)
{
    // handle method call com.deepin.anything.removeFileFromLFTBuf
    QStringList bufRootPathList;
    QMetaObject::invokeMethod(parent(), "removeFileFromLFTBuf", Q_RETURN_ARG(QStringList, bufRootPathList), Q_ARG(QByteArray, filePath));
    return bufRootPathList;
}

bool AnythingAdaptor::removePath(const QString &path)
{
    // handle method call com.deepin.anything.removePath
    bool success;
    QMetaObject::invokeMethod(parent(), "removePath", Q_RETURN_ARG(bool, success), Q_ARG(QString, path));
    return success;
}

QStringList AnythingAdaptor::renameFileOfLFTBuf(const QByteArray &fromFilePath, const QByteArray &toFilePath)
{
    // handle method call com.deepin.anything.renameFileOfLFTBuf
    QStringList bufRootPathList;
    QMetaObject::invokeMethod(parent(), "renameFileOfLFTBuf", Q_RETURN_ARG(QStringList, bufRootPathList), Q_ARG(QByteArray, fromFilePath), Q_ARG(QByteArray, toFilePath));
    return bufRootPathList;
}

QStringList AnythingAdaptor::search(int maxCount, qlonglong icase, uint startOffset, uint endOffset, const QString &path, const QString &keyword, bool useRegExp, uint &startOffset_, uint &endOffset_)
{
    // handle method call com.deepin.anything.search
    //return static_cast<YourObjectType *>(parent())->search(maxCount, icase, startOffset, endOffset, path, keyword, useRegExp, startOffset_, endOffset_);
}

QStringList AnythingAdaptor::search(const QString &path, const QString &keyword, bool useRegExp)
{
    // handle method call com.deepin.anything.search
    QStringList results;
    QMetaObject::invokeMethod(parent(), "search", Q_RETURN_ARG(QStringList, results), Q_ARG(QString, path), Q_ARG(QString, keyword), Q_ARG(bool, useRegExp));
    return results;
}

QByteArray AnythingAdaptor::setCodecNameForLocale(const QByteArray &name)
{
    // handle method call com.deepin.anything.setCodecNameForLocale
    QByteArray oldCodecName;
    QMetaObject::invokeMethod(parent(), "setCodecNameForLocale", Q_RETURN_ARG(QByteArray, oldCodecName), Q_ARG(QByteArray, name));
    return oldCodecName;
}

QStringList AnythingAdaptor::sync(const QString &mountPoint)
{
    // handle method call com.deepin.anything.sync
    QStringList rootPathList;
    QMetaObject::invokeMethod(parent(), "sync", Q_RETURN_ARG(QStringList, rootPathList), Q_ARG(QString, mountPoint));
    return rootPathList;
}

