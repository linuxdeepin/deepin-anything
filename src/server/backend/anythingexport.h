// Copyright (C) 2020 ~ 2021 Uniontech Software Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHINGEXPORT_H
#define ANYTHINGEXPORT_H

#include <QObject>

#define ANYTHINGBACKEND_SHARED_EXPORT Q_DECL_EXPORT
extern "C" ANYTHINGBACKEND_SHARED_EXPORT int fireAnything();
extern "C" ANYTHINGBACKEND_SHARED_EXPORT void downAnything();

#endif // ANYTHINGEXPORT_H
