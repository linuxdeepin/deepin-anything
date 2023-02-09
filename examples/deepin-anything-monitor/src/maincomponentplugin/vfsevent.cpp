// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vfsevent.h"

#include <QDebug>
#include <QTime>
#include <QVariant>

VfsEvent::VfsEvent() { this->time_ = QDateTime::currentDateTime(); }

int VfsEvent::operator==(const VfsEvent &other) {
  return this->cookie_ == other.cookie_ && this->act_ == other.act_ &&
         this->major_ == other.major_ && this->minor_ == other.minor_ &&
         this->src_ == other.src_;
}

QVariant VfsEvent::toVariant(int columns) const {
  // do not change the order
  switch (columns) {
    case 0:
      return QString("%1:%2").arg(this->major_).arg(this->minor_);
    case 1:
      return QVariant(this->act_);
    case 2:
      return this->src_;
    case 3:
      return this->dst_;
    case 4:
      return this->time_.toString();
  }
  return QVariant();
}

const char *VfsEvent::headerRow() { return "ID,Act,Src,Dst,Time"; }

QString VfsEvent::toRow() const {
  QString result;
  for (int col = 0; col < 5; col++) {
    if (col != 0) {
      result += ',';
    }
    result += toVariant(col).toString();
  }
  return std::move(result);
}
