// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <DAppLoader>
#include <QGuiApplication>

DQUICK_USE_NAMESPACE

int main(int argc, char *argv[])
{
#ifdef PLUGINPATH
    DAppLoader appLoader(APP_NAME, PLUGINPATH);
#else
    DAppLoader appLoader(APP_NAME);
#endif
    return appLoader.exec(argc, argv);
}
