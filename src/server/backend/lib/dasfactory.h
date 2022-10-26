// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DASFACTORY_H
#define DASFACTORY_H

#include <dasdefine.h>

#include <QStringList>

DAS_BEGIN_NAMESPACE

class DASInterface;
class DASPluginLoader;
class DASFactory
{
public:
    static QStringList keys();
    static DASInterface *create(const QString &key);
    static DASPluginLoader *loader();
};

DAS_END_NAMESPACE

#endif // DASFACTORY_H
