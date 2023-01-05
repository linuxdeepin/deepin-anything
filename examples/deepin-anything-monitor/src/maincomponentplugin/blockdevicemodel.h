// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BLOCKDEVICEMODEL_H
#define BLOCKDEVICEMODEL_H

#include "blockdeviceitem.h"

#include <QAbstractItemModel>
#include <QtQml>
#include <QSet>

class BlockDeviceModel : public QAbstractItemModel
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit BlockDeviceModel(QObject *parent = nullptr);

    enum BlockDeviceRoles
    {
        DevName = Qt::UserRole + 1,
        MajorId,
        MinorId,
        RmRole,
        SizeRole,
        RoRole,
        TypeRole,
        MountPointRole,
        FilterRole
    };

    // Basic functionality:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Q_INVOKABLE int getCheckState(const QModelIndex &index) const;
    Q_INVOKABLE bool check(const QModelIndex &index, bool checked);
    Q_INVOKABLE QString getDeviceName(QString id);
    QString getMountPoint(unsigned short major, unsigned short minor);

signals:
    void partitionUpdated();

public slots:
    Q_INVOKABLE void fetchDevices(bool isUpdate);
    void updatePartition();

private:
    QVector<BlockDeviceItem *> devices;

    BlockDeviceItem *rootItem;
    QSet<BlockDeviceItem *> checkedItems_;

    // QAbstractItemModel interface
public:
    QHash<int, QByteArray> roleNames() const;
    // QAbstractItemModel interface
public:
    QModelIndex index(int row, int column, const QModelIndex &parent) const;
    QModelIndex parent(const QModelIndex &child) const;
    int columnCount(const QModelIndex &parent) const;
    const QSet<BlockDeviceItem *> &checkedItems() const;
};

#endif // BLOCKDEVICEMODEL_H
