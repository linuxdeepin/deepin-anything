// SPDX-FileCopyrightText: 2022 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHINGBACKEND_H
#define ANYTHINGBACKEND_H

#include "dasdefine.h"
#include "eventsource.h"
#include <QObject>

DAS_BEGIN_NAMESPACE

class Server;
class AnythingBackend : public QObject
{
    Q_OBJECT
public:
    ~AnythingBackend();

    static AnythingBackend *instance();

    int init_connection()noexcept;

private:
    int monitorStart();
    int backendRun();

    Server *server = nullptr;
    bool hasconnected = false;
    EventSource *eventsrc = nullptr;
};

DAS_END_NAMESPACE

#endif // ANYTHINGBACKEND_H
