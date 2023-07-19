// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LFTMANAGER_H
#define LFTMANAGER_H

#include <QObject>
#include <QDBusContext>
#include <QTimer>

class DBlockDevice;
class LFTManager : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_PROPERTY(bool autoIndexInternal READ autoIndexInternal WRITE setAutoIndexInternal NOTIFY autoIndexInternalChanged)
    Q_PROPERTY(bool autoIndexExternal READ autoIndexExternal WRITE setAutoIndexExternal NOTIFY autoIndexExternalChanged)
    Q_PROPERTY(int logLevel READ logLevel WRITE setLogLevel)

public:
    ~LFTManager();

    static LFTManager *instance();
    static QString cacheDir();
    static QByteArray setCodecNameForLocale(const QByteArray &codecName);
    static void onFileChanged(QList<QPair<QByteArray, QByteArray>> &actionList);

    bool addPath(QString path, bool autoIndex = false);
    bool removePath(const QString &path);
    bool hasLFT(const QString &path) const;
    bool lftBuinding(const QString &path) const;
    bool cancelBuild(const QString &path);

    QStringList allPath() const;
    QStringList hasLFTSubdirectories(QString path) const;

    QStringList refresh(const QByteArray &serialUriFilter = QByteArray());
    QStringList sync(const QString &mountPoint = QString());

    QStringList search(const QString &path, const QString &keyword, bool useRegExp = false) const;
    QStringList search(int maxCount, qint64 icase, quint32 startOffset, quint32 endOffset,
                       const QString &path, const QString &keyword, bool useRegExp,
                       quint32 &startOffsetReturn, quint32 &endOffsetReturn) const;

    QStringList insertFileToLFTBuf(const QByteArray &file);
    QStringList removeFileFromLFTBuf(const QByteArray &file);
    QStringList renameFileOfLFTBuf(const QByteArray &oldFile, const QByteArray &newFIle);

    void quit();

    bool autoIndexExternal() const;
    bool autoIndexInternal() const;

    int logLevel() const;
    QStringList parallelsearch(const QString &path, const QString &keyword, const QStringList &rules) const;
    QStringList parallelsearch(const QString &path, quint32 startOffset, quint32 endOffset,
                               const QString &keyword, const QStringList &rules,
                               quint32 &startOffsetReturn, quint32 &endOffsetReturn) const;

public Q_SLOTS:
    void setAutoIndexExternal(bool autoIndexExternal);
    void setAutoIndexInternal(bool autoIndexInternal);

    void setLogLevel(int logLevel);

Q_SIGNALS:
    void addPathFinished(const QString &path, bool success);
    void autoIndexExternalChanged(bool autoIndexExternal);
    void autoIndexInternalChanged(bool autoIndexInternal);

    // 创建索引完成
    void buildFinished();

protected:
    explicit LFTManager(QObject *parent = nullptr);

    void sendErrorReply(QDBusError::ErrorType type, const QString &msg = QString()) const;

private:
    uint cpu_row_count;
    bool cpu_limited;
    QStringList building_paths;
    bool _isAutoIndexPartition() const;

    void _cpuLimitCheck();
    void _syncAll();
    void _indexAll(bool force = false);
    void _indexAllDelay();
    void _cleanAllIndex();
    void _addPathByPartition(const DBlockDevice *block);
    void onMountAdded(const QString &blockDevicePath, const QByteArray &mountPoint);
    void onMountRemoved(const QString &blockDevicePath, const QByteArray &mountPoint);
    void onFSAdded(const QString &blockDevicePath);
    void onFSRemoved(const QString &blockDevicePath);

    // search related private implementations.
    int _prepareBuf(quint32 *startOffset, quint32 *endOffset, const QString &path, void **buf, QString *newpath) const;
    int _separateSearchArgs(const QStringList &rules, bool *useRegExp, quint32 *startOffset, quint32 *endOffset, qint64 *maxTime, qint64 *maxCount) const;
    bool _getRuleArgs(const QStringList &rules, int searchFlag, quint32 &valueReturn) const;
    bool _getRuleStrings(const QStringList &rules, int searchFlag, QStringList &valuesReturn) const;
    bool _parseRules(void **prules, const QStringList &rules) const;
    QStringList _setRulesByDefault(const QStringList &rules, quint32 startOffset, quint32 endOffset) const;
    QStringList _enterSearch(const QString &path, const QString &keyword, const QStringList &rules, quint32 &startOffsetReturn, quint32 &endOffsetReturn) const;
    int _doSearch(void *vbuf, quint32 maxCount, const QString &path, const QString &keyword, quint32 *startOffset, quint32 *endOffset, QList<uint32_t> &results, const QStringList &rules = {}) const;
};

#endif // LFTMANAGER_H
