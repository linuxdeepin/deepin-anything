// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VFSEVENTMODEL_H
#define VFSEVENTMODEL_H

#include <qqml.h>

#include <QAbstractTableModel>

#include "blockdevicemodel.h"
#include "vfsevent.h"

class VfsEventModel : public QAbstractTableModel {
  Q_OBJECT
  QML_ELEMENT
  QML_ADDED_IN_MINOR_VERSION(1)
  // whether is running
  Q_PROPERTY(bool running READ isRunning WRITE setRunning NOTIFY runningChanged)

 public:
  enum VfsRole {
    IdRole = Qt::UserRole + 1,
    ActionRole,
    SrcRole,
    DstRole,
    TimeRole
  };

  explicit VfsEventModel(QObject *parent = nullptr);

  // Basic functionality:
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;

  Q_INVOKABLE static QString getReadableAction(int action);

  Q_INVOKABLE void setRunning(bool running);
  Q_INVOKABLE bool isRunning();
  Q_INVOKABLE bool isFiltered(int index);
  Q_INVOKABLE void filter(int index, bool checked);
  Q_INVOKABLE void exportToFile(QUrl path);
  Q_INVOKABLE void search(QString searchText);
  Q_INVOKABLE void setBlockDeviceModel(BlockDeviceModel *model);
  Q_INVOKABLE void clear();
  Q_INVOKABLE void resetHitModel();

 signals:
  void runningChanged();
  void filterChanged(int index, bool checked);

 public slots:
  void insertVfsEvent(const VfsEvent &evt);

 private:
  bool isHitFilter(const VfsEvent &evt);

  // backup events
  QVector<VfsEvent> evts_;
  // hit events
  QVector<VfsEvent> show_evts_;
  QString searchText_{""};
  QVector<bool> filtered_;
  bool running_{true};
  BlockDeviceModel *block_device_model_;

  // QAbstractItemModel interface
 public:
  QHash<int, QByteArray> roleNames() const;
};

#endif  // VFSEVENTMODEL_H
