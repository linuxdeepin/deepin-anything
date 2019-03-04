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
#include <QDBusInterface>
#include <QDBusConnection>

DAS_BEGIN_NAMESPACE

class UpdateLFTInterface : public DASInterface
{
public:
    UpdateLFTInterface(QObject *parent = nullptr)
        : DASInterface(parent)
        , interface("com.deepin.anything", "/com/deepin/anything",
                    "com.deepin.anything", QDBusConnection::systemBus())
    {
        // 如果更新的目标正在构建索引，将导致dbus调用阻塞，因此需要更长的超时时间
        // 此处设置为1个小时
        interface.setTimeout(60000 * 60);
    }

    void onFileCreate(const QByteArrayList &files) override
    {
        for (const QByteArray &f : files) {
            interface.call(QDBus::Block, "insertFileToLFTBuf", f);
        }
    }

    void onFileDelete(const QByteArrayList &files) override
    {
        for (const QByteArray &f : files) {
            interface.call(QDBus::Block, "removeFileFromLFTBuf", f);
        }
    }

    void onFileRename(const QList<QPair<QByteArray, QByteArray>> &files) override
    {
        for (const QPair<QByteArray, QByteArray> &f : files) {
            interface.call(QDBus::Block, "renameFileOfLFTBuf", f.first, f.second);
        }
    }

private:
    QDBusInterface interface;
};

class UpdateLFTPlugin : public DASPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DASFactoryInterface_iid FILE "update-lft.json")
public:
    DASInterface *create(const QString &key) override
    {
        Q_UNUSED(key)

        return new UpdateLFTInterface();
    }
};

DAS_END_NAMESPACE

#include "main.moc"
