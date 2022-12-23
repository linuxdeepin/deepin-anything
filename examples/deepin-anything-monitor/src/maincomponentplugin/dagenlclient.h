// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DAGENLCLIENT_H
#define DAGENLCLIENT_H

#include <QObject>
#include <QThread>
#include <DSingleton>

#include <deepin-anything/vfs_change_consts.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <QMap>

#include "vfsgenl.h"
#include "vfsevent.h"

class DAGenlClient : public QThread, public Dtk::Core::DSingleton<DAGenlClient>
{
    Q_OBJECT
    friend class DSingleton<DAGenlClient>;

public:
    /// @brief
    /// @param parent
    DAGenlClient();

    ~DAGenlClient() override;

    /// @brief init deepin anything genl client
    int init();

signals:
    void onVfsEvent(VfsEvent);
    void onPartitionUpdate();

protected:
    void run() override;

private:
    struct nl_cb *cb_;
    struct nl_sock *sock_;
    static int handleMsgFromGenl(struct nl_msg *msg, void *arg);

    QMap<unsigned int, QByteArray> rename_from_;

    int handleMsg(struct nl_msg *msg);
};

#endif // DAGENLCLIENT_H
