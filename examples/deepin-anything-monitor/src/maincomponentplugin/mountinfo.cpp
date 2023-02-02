// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mountinfo.h"

#include <QDebug>
#include <QFile>

MountInfo::MountInfo(QObject *parent) : QObject{parent} {
  this->fetchLatestMountInfo();
}

MountPoint MountInfo::queryMountPoint(const MaxMinString &min_max_string) {
  auto it = this->data_.find(min_max_string);
  if (it == this->data_.end()) {
    return "";
  }
  return it.value();
}

void MountInfo::fetchLatestMountInfo() {
  data_.clear();
  QFile mount_file{"/proc/self/mountinfo"};
  if (!mount_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }
  auto content = QString::fromStdString(mount_file.readAll().toStdString());
  auto lines = content.trimmed().split("\n");
  for (auto line : lines) {
    // 23 29 0:21 / /sys rw,nosuid,nodev,noexec,relatime shared:7 - sysfs sysfs
    auto items = line.trimmed().split(QRegExp("\\s"));
    if (items.empty() && items.length() < 5) {
      continue;
    }
    // Filter root data.
    if (items[3] != "/") {
      continue;
    }
    data_[items[2]] = items[4];
    qDebug() << items[2] << "->" << items[4];
  }
  mount_file.close();
}
