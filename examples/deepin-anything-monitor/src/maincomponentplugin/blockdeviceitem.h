// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BLOCKDEVICEITEM_H
#define BLOCKDEVICEITEM_H

#include <QString>
#include <QVariant>
#include <QVector>

/*
NAME MAJ:MIN RM SIZE RO TYPE MOUNTPOINTS
sda 8:0 0 64G 0 disk
sda1 8:1 0 64G 0 part /
sr0 11:0 1 50.5M 0 rom /media/kingtous/VBox_GAs_7.0.4
*/
enum { Name, Major, Minor, Rm, Size, RO, Type, MountPoints, __BDMAX };

#define BlockDeviceDataMAX __BDMAX - 1
#define ROOT "root"

/// This class indicating a block device
class BlockDeviceItem {
 public:
  explicit BlockDeviceItem(const QVector<QVariant> &data,
                           BlockDeviceItem *parent);
  ~BlockDeviceItem();

  void appendChild(BlockDeviceItem *child);

  BlockDeviceItem *child(int row);
  int childCount() const;
  int columnCount() const;
  QVariant data(int column) const;
  int row() const;
  BlockDeviceItem *parentItem();

  unsigned short getMajor() const;
  QString name() const;
  QString type() const;
  QString rootMountPoint() const;
  unsigned short getMinor() const;
  bool isPartition() const;

  void setChildItems(const QVector<BlockDeviceItem *> &newChildItems);

  const QVector<BlockDeviceItem *> &childItems() const;

 private:
  QVector<BlockDeviceItem *> childItems_{};
  QVector<QVariant> data_{};
  BlockDeviceItem *parentItem_{nullptr};

  bool is_checked_{false};
};

#endif  // BLOCKDEVICEITEM_H
