// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DASINTERFACE_H
#define DASINTERFACE_H

#include <dasdefine.h>

#include <QObject>

DAS_BEGIN_NAMESPACE

class DASInterface : public QObject
{
    Q_OBJECT
public:
    explicit DASInterface(QObject *parent = nullptr);

public Q_SLOTS:
    virtual void onFileCreate(const QByteArrayList &files) = 0;
    virtual void onFileDelete(const QByteArrayList &files) = 0;
    virtual void onFileRename(const QList<QPair<QByteArray, QByteArray>> &files) = 0;
};

DAS_END_NAMESPACE

#endif // DASINTERFACE_H
