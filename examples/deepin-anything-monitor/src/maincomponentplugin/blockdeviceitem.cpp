// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "blockdeviceitem.h"

BlockDeviceItem::BlockDeviceItem(const QVector<QVariant> &data,
                                 BlockDeviceItem *parent)
    : parentItem_(parent), data_(data) {}

BlockDeviceItem::~BlockDeviceItem() { qDeleteAll(childItems_); }

void BlockDeviceItem::appendChild(BlockDeviceItem *child)
{
  this->childItems_.push_back(child);
  child->parentItem_ = this;
}

BlockDeviceItem *BlockDeviceItem::child(int row)
{
  return this->childItems_[row];
}

int BlockDeviceItem::childCount() const { return this->childItems_.count(); }

int BlockDeviceItem::columnCount() const { return BlockDeviceDataMAX; }

QVariant BlockDeviceItem::data(int column) const { return this->data_[column]; }

int BlockDeviceItem::row() const
{
  if (parentItem_ != nullptr)
  {
    return parentItem_->childItems_.indexOf(
        const_cast<BlockDeviceItem *>(this));
  }
  return 0;
}

BlockDeviceItem *BlockDeviceItem::parentItem() { return this->parentItem_; }

unsigned short BlockDeviceItem::getMajor() const
{
  return this->data_[Major].toUInt();
}

QString BlockDeviceItem::name() const { return this->data_[Name].toString(); }

unsigned short BlockDeviceItem::getMinor() const
{
  return this->data_[Minor].toUInt();
}

bool BlockDeviceItem::isPartition() const
{
  return this->parentItem_ != nullptr && this->parentItem_->name() != ROOT;
}

void BlockDeviceItem::setChildItems(
    const QVector<BlockDeviceItem *> &newChildItems)
{
  if (!childItems_.empty())
  {
    childItems_.clear();
  }
  for (auto elem : newChildItems)
  {
    elem->parentItem_ = this;
    childItems_.push_back(elem);
  }
}
