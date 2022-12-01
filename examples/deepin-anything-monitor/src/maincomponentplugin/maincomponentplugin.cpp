// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "maincomponentplugin.h"
#include "dagenlclient.h"

#include <QUrl>
#include <QDebug>

DQUICK_USE_NAMESPACE

MainComponentPlugin::MainComponentPlugin(QObject *parent)
    : QObject(parent)
{
    int ret;
    if((ret = DAGenlClient::ref().init())) {
        qErrnoWarning(ret, "genl client initialize failed.");
    }
}

MainComponentPlugin::~MainComponentPlugin()
{

}

QUrl MainComponentPlugin::mainComponentPath() const
{
    // 返回程序的主控件部分 qml 文件，请确保该文件存在
    return QUrl("qrc:///main.qml");
}
