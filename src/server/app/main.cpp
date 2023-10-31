// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCoreApplication>
#include <QDebug>
#include "anythingexport.h"
#include <signal.h>

static void handleSIGTERM(int sig)
{
    qDebug() << "received SIGTERM" << sig;
    if (qApp) {
        qApp->quit();
    }
}

int main(int argc, char *argv[])
{
    int ret;

    QCoreApplication app(argc, argv);
    app.setOrganizationName("deepin");

    if (fireAnything()) {
        qCritical() << "fireAnything failed!";
        abort();
    }
    signal(SIGTERM, handleSIGTERM);

    ret = app.exec();
    downAnything();
    return ret;
}
