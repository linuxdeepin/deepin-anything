// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "blockdevicemodel.h"

#include <QAbstractItemModelTester>
#include <QDebug>
#include <QModelIndex>
#include <QProcess>

#include "dagenlclient.h"
#include "mountinfo.h"

BlockDeviceModel::BlockDeviceModel(QObject *parent)
    : QAbstractItemModel(parent) {
  rootItem = new BlockDeviceItem({ROOT}, nullptr);
  fetchDevices(false);
  connect(&DAGenlClient::ref(), SIGNAL(onPartitionUpdate()), this,
          SLOT(updatePartition()));
}

int BlockDeviceModel::rowCount(const QModelIndex &parent) const {
  BlockDeviceItem *parentItem;

  if (parent.column() > 0) return 0;

  if (!parent.isValid())
    parentItem = rootItem;
  else
    parentItem = static_cast<BlockDeviceItem *>(parent.internalPointer());
  return parentItem->childCount();
}

QVariant BlockDeviceModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) {
    return QVariant();
  }
  auto device = static_cast<BlockDeviceItem *>(index.internalPointer());
  if (role == BlockDeviceRoles::FilterRole) {
    return getCheckState(index);
  }
  if (role == BlockDeviceRoles::MountPointRole) {
    auto mnt = device->data(role - BlockDeviceRoles::DevName);
    return mnt.toString().replace("\\x0a", " ");
  }
  if (role >= BlockDeviceRoles::DevName) {
    return device->data(role - BlockDeviceRoles::DevName);
  }
  if (role == Qt::DisplayRole && index.isValid()) {
    return device->data(Name);
  }
  if (role == Qt::TextAlignmentRole) {
    return QVariant(Qt::AlignLeft);
  }

  return QVariant();
}

int BlockDeviceModel::getCheckState(const QModelIndex &index) const {
  if (!index.isValid()) {
    return false;
  }
  auto item = static_cast<BlockDeviceItem *>(index.internalPointer());
  qDebug() << "check" << item->name();
  if (item->isPartition()) {
    return checkedItems_.contains(item) ? Qt::CheckState::Checked
                                        : Qt::CheckState::Unchecked;
  } else {
    auto checkCnt = 0;
    for (auto *checkedItem : checkedItems_) {
      if (checkedItem->parentItem() == item) {
        checkCnt++;
      }
    }
    if (checkCnt == item->childCount()) {
      return Qt::CheckState::Checked;
    } else if (checkCnt == 0) {
      return Qt::CheckState::Unchecked;
    } else {
      return Qt::CheckState::PartiallyChecked;
    }
  }
}

bool BlockDeviceModel::check(const QModelIndex &index, bool checked) {
  if (!index.isValid()) {
    return false;
  }
  auto item = static_cast<BlockDeviceItem *>(index.internalPointer());
  if (!item->isPartition()) {
    auto cnt = item->childCount();
    for (auto row = 0; row < cnt; row++) {
      auto partition = item->child(row);
      if (checked) {
        checkedItems_.insert(partition);
      } else {
        checkedItems_.remove(partition);
      }
    }
  }
  // self
  if (checked) {
    checkedItems_.insert(item);
  } else {
    checkedItems_.remove(item);
  }
  qDebug() << "listening" << checkedItems_;
  // update parent
  emit layoutChanged({index});
  emit dataChanged(index, index, {BlockDeviceModel::FilterRole});
  return true;
}

QString BlockDeviceModel::getDeviceName(QString id) {
  auto id_list = id.split(":");
  if (id_list.count() == 2) {
    int major = id_list[0].toInt();
    int minor = id_list[1].toInt();
    for (auto &root : devices) {
      auto cnt = root->childCount();
      for (int i = 0; i < cnt; i++) {
        auto child = root->child(i);
        if (child->getMajor() == major && child->getMinor() == minor) {
          return child->name();
        }
      }
    }
  }

  return tr("Unknown");
}

QString BlockDeviceModel::getMountPoint(unsigned short major,
                                        unsigned short minor) {
  for (auto &root : devices) {
    if (root->getMajor() == major && root->getMinor() == minor) {
      return root->rootMountPoint();
    }
    auto child_count = root->childCount();
    if (child_count != 0) {
      for (int row = 0; row < child_count; row++) {
        auto child = root->child(row);
        if (child->getMajor() == major && child->getMinor() == minor) {
          return child->rootMountPoint();
        }
      }
    }
  }
  return "";
}

void BlockDeviceModel::fetchDevices(bool isUpdate = true) {
  // get all data
  auto process = new QProcess();
  process->start("/usr/bin/lsblk", {"-r"});
  if (!process->waitForFinished()) {
    qDebug() << "error on waiting lsblk commands";
    return;
  }
  auto blk_output = process->readAll();
  auto blk_string = QString::fromUtf8(blk_output);
  auto blk_list = blk_string.split("\n");
  blk_list.erase(blk_list.begin());
  if (isUpdate) {
    if (rowCount()) {
      beginRemoveRows(QModelIndex(), 0, rowCount() - 1);
      devices.clear();
      endRemoveRows();
    }
  }
  QVector<BlockDeviceItem *> device_repo;
  for (auto &blk : blk_list) {
    if (blk.trimmed().isEmpty()) {
      continue;
    }
    auto dev_metadata = blk.trimmed().split(" ");
    auto data_vec = QVector<QVariant>(BlockDeviceDataMAX + 1, QVariant());
    // block device name
    data_vec[Name] = QVariant(std::move(dev_metadata[0]));
    // major minor id
    auto maj_min_list = QString(dev_metadata[1]).split(":");
    auto major = maj_min_list[0];
    data_vec[Major] = maj_min_list[0].toInt();
    data_vec[Minor] = maj_min_list[1].toInt();
    // other meta
    data_vec[Rm] = dev_metadata[2].toInt();
    data_vec[Size] = std::move(dev_metadata[3]);
    data_vec[RO] = dev_metadata[4].toInt();
    data_vec[Type] = std::move(dev_metadata[5]);
    auto mnt_point =
        dev_metadata.count() < BlockDeviceDataMAX ? "" : dev_metadata[6];
    data_vec[MountPoints] = mnt_point;
    auto item = new BlockDeviceItem{std::move(data_vec), nullptr};
    device_repo.push_back(item);
    if (!isUpdate) {
      // checked
      checkedItems_.insert(item);
    }
  }
  // build root tree
  QVector<BlockDeviceItem *> root_devices;
  for (auto &dev : device_repo) {
    if (dev->type() != "part") {
      root_devices.push_back(dev);
    }
  }
  for (auto &root : root_devices) {
    auto root_name = root->name();
    for (auto &dev : device_repo) {
      if (root != dev && dev->name().startsWith(root_name)) {
        root->appendChild(dev);
      }
    }
  }
  if (isUpdate) {
    beginResetModel();
    for (auto root : rootItem->childItems()) {
      delete root;
    }
    rootItem->setChildItems(root_devices);
    devices = std::move(root_devices);
    checkedItems_.clear();
    for (auto dev : devices) {
      checkedItems_.insert(dev);
      for (auto child : dev->childItems()) {
        checkedItems_.insert(child);
      }
    }
    endResetModel();
  } else {
    rootItem->setChildItems(root_devices);
    devices.append(root_devices);
  }
  qDebug() << "Got " << devices.count() << " devices";
}

void BlockDeviceModel::updatePartition() {
  fetchDevices(true);
  MountInfo::ref().fetchLatestMountInfo();
  emit partitionUpdated();
}

QHash<int, QByteArray> BlockDeviceModel::roleNames() const {
  return {{BlockDeviceRoles::DevName, "name"},
          {BlockDeviceRoles::MajorId, "major"},
          {BlockDeviceRoles::MinorId, "minor"},
          {BlockDeviceRoles::RmRole, "rm"},
          {BlockDeviceRoles::SizeRole, "size"},
          {BlockDeviceRoles::RoRole, "ro"},
          {BlockDeviceRoles::TypeRole, "type"},
          {BlockDeviceRoles::MountPointRole, "mount"}};
}

QModelIndex BlockDeviceModel::index(int row, int column,
                                    const QModelIndex &parent) const {
  if (!hasIndex(row, column, parent)) {
    return QModelIndex();
  }
  BlockDeviceItem *parentItem;
  if (!parent.isValid()) {
    parentItem = rootItem;
  } else {
    parentItem = static_cast<BlockDeviceItem *>(parent.internalPointer());
  }
  auto childItem = parentItem->child(row);
  if (childItem) {
    return createIndex(row, column, childItem);
  }
  return QModelIndex();
}

QModelIndex BlockDeviceModel::parent(const QModelIndex &child) const {
  if (!child.isValid()) {
    return QModelIndex();
  }
  BlockDeviceItem *childItem =
      static_cast<BlockDeviceItem *>(child.internalPointer());
  if (childItem == rootItem) {
    return QModelIndex();
  }
  auto parentItem = childItem->parentItem();

  return createIndex(parentItem->row(), 0, parentItem);
}

int BlockDeviceModel::columnCount(const QModelIndex &parent) const { return 3; }

const QSet<BlockDeviceItem *> &BlockDeviceModel::checkedItems() const {
  return checkedItems_;
}
