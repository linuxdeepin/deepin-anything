// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VFSEVENT_H
#define VFSEVENT_H

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTime>

class VfsEvent {
 public:
  explicit VfsEvent();

  unsigned char act_;
  QString src_;
  QString dst_;
  unsigned int cookie_;
  unsigned short major_;
  unsigned char minor_;
  QDateTime time_;

  int operator==(const VfsEvent &other);

  QVariant toVariant(int columns) const;
  static const char *headerRow();
  QString toRow() const;
};

#endif  // VFSEVENT_H
