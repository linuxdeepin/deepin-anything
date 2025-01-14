// SPDX-FileCopyrightText: 2024 - 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHINGADAPTOR_H
#define ANYTHINGADAPTOR_H

#include <QtCore/QObject>
#include <QtDBus/QtDBus>
QT_BEGIN_NAMESPACE
class QByteArray;
template<class T> class QList;
template<class Key, class Value> class QMap;
class QString;
class QStringList;
class QVariant;
QT_END_NAMESPACE

/*
 * Adaptor class for interface com.deepin.anything
 */
class AnythingAdaptor: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.deepin.anything")
    Q_CLASSINFO("D-Bus Introspection", ""
"  <interface name=\"com.deepin.anything\">\n"
"    <property access=\"readwrite\" type=\"b\" name=\"autoIndexInternal\"/>\n"
"    <property access=\"readwrite\" type=\"b\" name=\"autoIndexExternal\"/>\n"
"    <property access=\"readwrite\" type=\"i\" name=\"logLevel\"/>\n"
"    <method name=\"cacheDir\">\n"
"      <arg direction=\"out\" type=\"s\" name=\"path\"/>\n"
"    </method>\n"
"    <method name=\"setCodecNameForLocale\">\n"
"      <arg direction=\"in\" type=\"ay\" name=\"name\"/>\n"
"      <arg direction=\"out\" type=\"ay\" name=\"oldCodecName\"/>\n"
"    </method>\n"
"    <method name=\"allPath\">\n"
"      <arg direction=\"out\" type=\"as\" name=\"pathList\"/>\n"
"    </method>\n"
"    <method name=\"addPath\">\n"
"      <arg direction=\"in\" type=\"s\" name=\"path\"/>\n"
"      <arg direction=\"out\" type=\"b\" name=\"success\"/>\n"
"    </method>\n"
"    <method name=\"removePath\">\n"
"      <arg direction=\"in\" type=\"s\" name=\"path\"/>\n"
"      <arg direction=\"out\" type=\"b\" name=\"success\"/>\n"
"    </method>\n"
"    <method name=\"hasLFT\">\n"
"      <arg direction=\"in\" type=\"s\" name=\"path\"/>\n"
"      <arg direction=\"out\" type=\"b\" name=\"success\"/>\n"
"    </method>\n"
"    <method name=\"hasLFTSubdirectories\">\n"
"      <arg direction=\"in\" type=\"s\" name=\"path\"/>\n"
"      <arg direction=\"out\" type=\"as\" name=\"directories\"/>\n"
"    </method>\n"
"    <method name=\"lftBuinding\">\n"
"      <arg direction=\"in\" type=\"s\" name=\"path\"/>\n"
"      <arg direction=\"out\" type=\"b\" name=\"success\"/>\n"
"    </method>\n"
"    <method name=\"cancelBuild\">\n"
"      <arg direction=\"in\" type=\"s\" name=\"path\"/>\n"
"      <arg direction=\"out\" type=\"b\" name=\"success\"/>\n"
"    </method>\n"
"    <method name=\"refresh\">\n"
"      <arg direction=\"in\" type=\"ay\" name=\"serialUriFilter\"/>\n"
"      <arg direction=\"out\" type=\"as\" name=\"rootPathList\"/>\n"
"    </method>\n"
"    <method name=\"sync\">\n"
"      <arg direction=\"in\" type=\"s\" name=\"mountPoint\"/>\n"
"      <arg direction=\"out\" type=\"as\" name=\"rootPathList\"/>\n"
"    </method>\n"
"    <method name=\"search\">\n"
"      <arg direction=\"in\" type=\"s\" name=\"path\"/>\n"
"      <arg direction=\"in\" type=\"s\" name=\"keyword\"/>\n"
"      <arg direction=\"in\" type=\"b\" name=\"useRegExp\"/>\n"
"      <arg direction=\"out\" type=\"as\" name=\"results\"/>\n"
"    </method>\n"
"    <method name=\"search\">\n"
"      <arg direction=\"in\" type=\"i\" name=\"maxCount\"/>\n"
"      <arg direction=\"in\" type=\"x\" name=\"icase\"/>\n"
"      <arg direction=\"in\" type=\"u\" name=\"startOffset\"/>\n"
"      <arg direction=\"in\" type=\"u\" name=\"endOffset\"/>\n"
"      <arg direction=\"in\" type=\"s\" name=\"path\"/>\n"
"      <arg direction=\"in\" type=\"s\" name=\"keyword\"/>\n"
"      <arg direction=\"in\" type=\"b\" name=\"useRegExp\"/>\n"
"      <arg direction=\"out\" type=\"as\" name=\"results\"/>\n"
"      <arg direction=\"out\" type=\"u\" name=\"startOffset\"/>\n"
"      <arg direction=\"out\" type=\"u\" name=\"endOffset\"/>\n"
"    </method>\n"
"    <method name=\"parallelsearch\">\n"
"      <arg direction=\"in\" type=\"s\" name=\"path\"/>\n"
"      <arg direction=\"in\" type=\"s\" name=\"keyword\"/>\n"
"      <arg direction=\"in\" type=\"as\" name=\"rules\"/>\n"
"      <arg direction=\"out\" type=\"as\" name=\"results\"/>\n"
"    </method>\n"
"    <method name=\"parallelsearch\">\n"
"      <arg direction=\"in\" type=\"s\" name=\"path\"/>\n"
"      <arg direction=\"in\" type=\"u\" name=\"startOffset\"/>\n"
"      <arg direction=\"in\" type=\"u\" name=\"endOffset\"/>\n"
"      <arg direction=\"in\" type=\"s\" name=\"keyword\"/>\n"
"      <arg direction=\"in\" type=\"as\" name=\"rules\"/>\n"
"      <arg direction=\"out\" type=\"as\" name=\"results\"/>\n"
"      <arg direction=\"out\" type=\"u\" name=\"startOffset\"/>\n"
"      <arg direction=\"out\" type=\"u\" name=\"endOffset\"/>\n"
"    </method>\n"
"    <method name=\"insertFileToLFTBuf\">\n"
"      <arg direction=\"in\" type=\"ay\" name=\"filePath\"/>\n"
"      <arg direction=\"out\" type=\"as\" name=\"bufRootPathList\"/>\n"
"    </method>\n"
"    <method name=\"removeFileFromLFTBuf\">\n"
"      <arg direction=\"in\" type=\"ay\" name=\"filePath\"/>\n"
"      <arg direction=\"out\" type=\"as\" name=\"bufRootPathList\"/>\n"
"    </method>\n"
"    <method name=\"renameFileOfLFTBuf\">\n"
"      <arg direction=\"in\" type=\"ay\" name=\"fromFilePath\"/>\n"
"      <arg direction=\"in\" type=\"ay\" name=\"toFilePath\"/>\n"
"      <arg direction=\"out\" type=\"as\" name=\"bufRootPathList\"/>\n"
"    </method>\n"
"    <method name=\"quit\"/>\n"
"    <signal name=\"addPathFinished\">\n"
"      <arg type=\"s\" name=\"path\"/>\n"
"      <arg type=\"b\" name=\"success\"/>\n"
"    </signal>\n"
"  </interface>\n"
        "")
public:
    AnythingAdaptor(QObject *parent);
    virtual ~AnythingAdaptor();

public: // PROPERTIES
    Q_PROPERTY(bool autoIndexExternal READ autoIndexExternal WRITE setAutoIndexExternal)
    bool autoIndexExternal() const;
    void setAutoIndexExternal(bool value);

    Q_PROPERTY(bool autoIndexInternal READ autoIndexInternal WRITE setAutoIndexInternal)
    bool autoIndexInternal() const;
    void setAutoIndexInternal(bool value);

    Q_PROPERTY(int logLevel READ logLevel WRITE setLogLevel)
    int logLevel() const;
    void setLogLevel(int value);

public Q_SLOTS: // METHODS
    bool addPath(const QString &path);
    QStringList allPath();
    QString cacheDir();
    bool cancelBuild(const QString &path);
    bool hasLFT(const QString &path);
    QStringList hasLFTSubdirectories(const QString &path);
    QStringList insertFileToLFTBuf(const QByteArray &filePath);
    bool lftBuinding(const QString &path);
    QStringList parallelsearch(const QString &path, uint startOffset, uint endOffset, const QString &keyword, const QStringList &rules, uint &startOffset_, uint &endOffset_);
    QStringList parallelsearch(const QString &path, const QString &keyword, const QStringList &rules);
    void quit();
    QStringList refresh(const QByteArray &serialUriFilter);
    QStringList removeFileFromLFTBuf(const QByteArray &filePath);
    bool removePath(const QString &path);
    QStringList renameFileOfLFTBuf(const QByteArray &fromFilePath, const QByteArray &toFilePath);
    QStringList search(int maxCount, qlonglong icase, uint startOffset, uint endOffset, const QString &path, const QString &keyword, bool useRegExp, uint &startOffset_, uint &endOffset_);
    QStringList search(const QString &path, const QString &keyword, bool useRegExp);
    QByteArray setCodecNameForLocale(const QByteArray &name);
    QStringList sync(const QString &mountPoint);
Q_SIGNALS: // SIGNALS
    void addPathFinished(const QString &path, bool success);
};

#endif
