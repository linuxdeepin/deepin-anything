// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SERVER_H
#define SERVER_H

#include "dasdefine.h"
#include "eventsource.h"
#include "eventadaptor.h"

#include <QThread>

DAS_BEGIN_NAMESPACE

class Server : public QThread
{
    Q_OBJECT

public:
    explicit Server(EventSource *eventsrc, QObject *parent = nullptr);
    void setEventAdaptor(EventAdaptor *adaptor);

    static QStringList logCategoryList();

private:
    void run() override;

private:
    EventSource *eventsrc;
    EventAdaptor *eventAdaptor = nullptr;
};

DAS_END_NAMESPACE

#endif // SERVER_H
