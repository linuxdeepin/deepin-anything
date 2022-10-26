// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DASPLUGIN_H
#define DASPLUGIN_H

#include <dasdefine.h>

#include <QObject>

DAS_BEGIN_NAMESPACE
#define DASFactoryInterface_iid "com.deepin.anything.server.DASFactoryInterface_iid"

class DASInterface;
class DASPlugin : public QObject
{
    Q_OBJECT
public:
    explicit DASPlugin(QObject *parent = nullptr);

    virtual DASInterface *create(const QString &key) = 0;
};

DAS_END_NAMESPACE

#endif // DASPLUGIN_H
