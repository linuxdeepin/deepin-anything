/*
 * Copyright (C) 2017 ~ 2019 Deepin Technology Co., Ltd.
 *
 * Author:     zccrs <zccrs@live.com>
 *
 * Maintainer: zccrs <zhangjide@deepin.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDBusConnection>

#include "lftmanager.h"
#include "anything_adaptor.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    app.setOrganizationName("deepin");

    QCommandLineOption option_dbus("dbus", "Start on DBus mode.");
    QCommandLineParser parser;

    parser.addOption(option_dbus);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    if (parser.isSet(option_dbus)) {
        if (!QDBusConnection::systemBus().isConnected()) {
            qWarning("Cannot connect to the D-Bus session bus.\n"
                     "Please check your system settings and try again.\n");
            return 1;
        }

        // add our D-Bus interface and connect to D-Bus
        if (!QDBusConnection::systemBus().registerService("com.deepin.anything")) {
            qWarning("Cannot register the \"com.deepin.anything\" service.\n");
            return 2;
        }

        Q_UNUSED(new AnythingAdaptor(LFTManager::instance()));

        if (!QDBusConnection::systemBus().registerObject("/com/deepin/anything", LFTManager::instance())) {
            qWarning("Cannot register to the D-Bus object: \"/com/deepin/anything\"\n");
            return 3;
        }
    } else {
        return 0;
    }

    return app.exec();
}
