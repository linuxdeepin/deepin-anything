// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <DAppLoader>
#include <QGuiApplication>

DQUICK_USE_NAMESPACE

int main(int argc, char *argv[]) {
  // workaround: fix popup menu
  qputenv("D_POPUP_MODE", "embed");
#ifdef PLUGINPATH
  DAppLoader appLoader(APP_NAME, PLUGINPATH);
#else
  DAppLoader appLoader(APP_NAME);
#endif
  return appLoader.exec(argc, argv);
}
