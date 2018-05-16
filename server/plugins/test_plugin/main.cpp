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

#include "dasplugin.h"
#include "dasinterface.h"

#include <QDebug>
#include <QCoreApplication>

DAS_BEGIN_NAMESPACE

class TestInterface : public DASInterface
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

class TestPlugin : public DASPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DASFactoryInterface_iid FILE "test.json")
public:
    DASInterface *create(const QString &key) override
    {
        Q_UNUSED(key)

        return new TestInterface();
    }
};

DAS_END_NAMESPACE

#include "main.moc"
