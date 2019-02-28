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

#include "lftmanager.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    app.setOrganizationName("deepin");

    QObject::connect(LFTManager::instance(), &LFTManager::addPathFinished, [] (const QString &path, bool ok) {
        qDebug() << path << ok;
    });

//    qDebug() << LFTManager::instance()->hasLFT("/media/deepin/test");
//    qDebug() << LFTManager::instance()->search("/media/deepin/7abe0e5f-55d8-4a16-95b6-049910a7f00a", "dee");
//    qDebug() << LFTManager::instance()->search("/media/deepin/test", "dee");
//    qDebug() << LFTManager::instance()->addPath("/media/deepin/7abe0e5f-55d8-4a16-95b6-049910a7f00a");
//    if (!LFTManager::instance()->hasLFT("/tmp"))
//        qDebug() << LFTManager::instance()->addPath("/tmp");
//    else
//        qDebug() << LFTManager::instance()->search("/tmp", "deepin", false);

    QTimer::singleShot(1000, &app, &QCoreApplication::quit);

    return app.exec();
}
