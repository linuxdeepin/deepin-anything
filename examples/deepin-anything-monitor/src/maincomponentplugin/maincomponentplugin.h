// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MAINCOMPONENTPLUGIN_H
#define MAINCOMPONENTPLUGIN_H

#include <dqmlappmainwindowinterface.h>

class QQmlComponent;
class MainComponentPlugin : public QObject, public DTK_QUICK_NAMESPACE::DQmlAppMainWindowInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DQmlAppMainWindowInterface_iid)
    Q_INTERFACES(DTK_QUICK_NAMESPACE::DQmlAppMainWindowInterface)
public:
    MainComponentPlugin(QObject *parent = nullptr);
    ~MainComponentPlugin() override;

    void initialize(QQmlApplicationEngine *engine) override;

    QUrl mainComponentPath() const override;
};

#endif // MAINCOMPONENTPLUGIN_H
