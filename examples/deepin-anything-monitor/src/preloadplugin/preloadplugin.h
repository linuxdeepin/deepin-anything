// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PRELOADPLUGIN_H
#define PRELOADPLUGIN_H

#include <dqmlapppreloadinterface.h>

class QQmlComponent;
class PreloadPlugin : public QObject, public DTK_QUICK_NAMESPACE::DQmlAppPreloadInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DQmlAppPreloadInterface_iid)
    Q_INTERFACES(DTK_QUICK_NAMESPACE::DQmlAppPreloadInterface)
public:
    PreloadPlugin(QObject *parent = nullptr);
    ~PreloadPlugin() override;

    virtual QUrl preloadComponentPath() const override;
};

#endif // PRELOADPLUGIN_H
