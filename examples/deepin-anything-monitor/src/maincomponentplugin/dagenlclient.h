#ifndef DAGENLCLIENT_H
#define DAGENLCLIENT_H

#include <QObject>
#include <QThread>
#include <DSingleton>

#include <deepin-anything/vfs_change_consts.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>

#include "vfs_genl.h"
#include "vfs_event.h"

class DAGenlClient : public QThread, public Dtk::Core::DSingleton<DAGenlClient>
{
    friend class DSingleton<DAGenlClient>;

public:
    /// @brief
    /// @param parent
    DAGenlClient();

    ~DAGenlClient();

    /// @brief init deepin anything genl client
    int init();

signals:
    void onVfsEvent(VfsEvent);

protected:
    void run() override;

private:
    struct nl_cb *cb_;
    struct nl_sock *sock_;

    static int handleMsgFromGenl(struct nl_msg *msg, void *arg);

    int handleMsg(struct nl_msg* msg);
};

#endif // DAGENLCLIENT_H
