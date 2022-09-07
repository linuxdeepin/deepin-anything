// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dasplugin.h"
#include "dasinterface.h"

#include <QDebug>
#include <QCoreApplication>

DAS_BEGIN_NAMESPACE

class UpdateLFTInterface : public DASInterface
{
public:
    void onFileCreate(const QByteArrayList &files) override
    {
       for (const QByteArray &f : files)
           qDebug() << __FUNCTION__ << thread() << qApp->thread() << QString::fromLocal8Bit(f);
    }

    void onFileDelete(const QByteArrayList &files) override
    {
        for (const QByteArray &f : files)
            qDebug() << __FUNCTION__ << thread() << qApp->thread() << QString::fromLocal8Bit(f);
    }

    void onFileRename(const QList<QPair<QByteArray, QByteArray>> &files) override
    {
        for (const QPair<QByteArray, QByteArray> &f : files)
            qDebug() << __FUNCTION__ << thread() << qApp->thread() << QString::fromLocal8Bit(f.first) << QString::fromLocal8Bit(f.second);
    }
};

class UpdateLFTPlugin : public DASPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DASFactoryInterface_iid FILE "test.json")
public:
    DASInterface *create(const QString &key) override
    {
        Q_UNUSED(key)

        return new UpdateLFTInterface();
    }
};

DAS_END_NAMESPACE

#include "main.moc"
