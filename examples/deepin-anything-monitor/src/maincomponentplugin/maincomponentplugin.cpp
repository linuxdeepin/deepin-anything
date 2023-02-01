// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "maincomponentplugin.h"

#include <QDebug>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>

#include "blockdevicemodel.h"
#include "dagenlclient.h"
#include "mountinfo.h"
#include "vfsevent.h"
#include "vfseventmodel.h"

DQUICK_USE_NAMESPACE

MainComponentPlugin::MainComponentPlugin(QObject *parent) : QObject(parent) {
  int ret;
  if ((ret = DAGenlClient::ref().init())) {
    qErrnoWarning(ret, "genl client initialize failed.");
  }
}

MainComponentPlugin::~MainComponentPlugin() {
  DAGenlClient::ref().terminate();
  // wait for one second to wait the thread being terminated.
  DAGenlClient::ref().wait(QDeadlineTimer(1000));
}

void MainComponentPlugin::initialize(QQmlApplicationEngine *engine) {
  qmlRegisterType<BlockDeviceModel>("com.kingtous.block_device_model", 1, 0,
                                    "BlockDeviceModel");
  qmlRegisterType<VfsEventModel>("com.kingtous.vfs_event_model", 1, 0,
                                 "VfsEventModel");
  engine->rootContext()->setContextProperty("genl_client",
                                            &DAGenlClient::ref());
  engine->rootContext()->setContextProperty("mount_info", &MountInfo::ref());
  qRegisterMetaType<VfsEvent>("VfsEvent");
}

QUrl MainComponentPlugin::mainComponentPath() const {
  // 返回程序的主控件部分 qml 文件，请确保该文件存在
  return QUrl("qrc:///main.qml");
}
