// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EVENTSOURCE_H
#define EVENTSOURCE_H

#include "dasdefine.h"

DAS_BEGIN_NAMESPACE

class EventSource
{
public:
    virtual ~EventSource() = 0;
    virtual bool init() = 0;
    virtual bool isInited() = 0;
    virtual bool getEvent(unsigned char *type, char **src, char **dst, bool *end) = 0;
};

DAS_END_NAMESPACE

#endif // EVENTSOURCE_H
