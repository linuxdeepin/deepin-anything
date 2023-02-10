// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MOUNTINFO_H
#define MOUNTINFO_H

#include <DSingleton>
#include <QMap>
#include <QObject>

typedef QString MaxMinString;
typedef QString MountPoint;

class MountInfo : public QObject, public Dtk::Core::DSingleton<MountInfo> {
  Q_OBJECT
  friend class Dtk::Core::DSingleton<MountInfo>;

 public:
  explicit MountInfo(QObject* parent = nullptr);
  MountPoint queryMountPoint(const MaxMinString& min_max_string);
  void fetchLatestMountInfo();

 signals:

 private:
  // <MAX:MIN, ROOT_MOUNTPOINT
  QMap<MaxMinString, MountPoint> data_;
};

#endif  // MOUNTINFO_H
