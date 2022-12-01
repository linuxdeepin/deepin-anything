#include "dagenlclient.h"

#include "vfs_genl.h"
#include <libnl3/netlink/netlink.h>

#include <QDebug>

#define get_attr(attrs, ATTR, attr, type) \
    if (!attrs[ATTR]) { \
        return 0; \
    } \
    attr = nla_get_##type(attrs[ATTR])

/* major, minor*/
#define MKDEV(ma,mi)    ((ma)<<8 | (mi))

/* attribute policy */
static struct nla_policy vfsnotify_genl_policy[VFSMONITOR_A_MAX + 1];

DAGenlClient::DAGenlClient() : QThread(nullptr) {

};

DAGenlClient::~DAGenlClient() {
    if (cb_){
        nl_cb_put(cb_);
        cb_ = nullptr;
    }
    if (sock_) {
        nl_socket_free(sock_);
        sock_ = nullptr;
    }
}

int DAGenlClient::handleMsgFromGenl(struct nl_msg *msg, void *arg) {
    DAGenlClient* client = (DAGenlClient*) arg;
    return client->handleMsg(msg);
}

int DAGenlClient::handleMsg(struct nl_msg* msg) {
    struct nlattr* attrs[VFSMONITOR_A_MAX + 1];
    int ret = genlmsg_parse(nlmsg_hdr(msg), 0, attrs, VFSMONITOR_A_MAX, vfsnotify_genl_policy);
    if (ret < 0) {
        printf("error parse genl msg\n");
        return 1;
    }

    unsigned char act;
    char *src;
    unsigned int _cookie;
    unsigned short major = 0;
    unsigned char minor = 0;

    get_attr(attrs, VFSMONITOR_A_ACT, act, u8);
    get_attr(attrs, VFSMONITOR_A_COOKIE, _cookie, u32);
    get_attr(attrs, VFSMONITOR_A_MAJOR, major, u16);
    get_attr(attrs, VFSMONITOR_A_MINOR, minor, u8);
    get_attr(attrs, VFSMONITOR_A_PATH, src, string);

    // send signal
    VfsEvent evt{};
    evt.act = act;
    evt.major = major;
    evt.minor = minor;
    evt.src = src;
    emit onVfsEvent(evt);
    return 0;
}

int DAGenlClient::init() {
    int family_id;

    sock_ = nl_socket_alloc();
    if (!sock_) {
        fprintf(stderr, "error on nl_socket_alloc\n");
        exit(-1);
        return -1;
    }

    cb_ = nl_cb_alloc(NL_CB_DEFAULT);
    if (!cb_)
    {
        fprintf(stderr, "error on nl_cb_alloc\n");
        return -1;
    }

    nl_socket_disable_seq_check(this->sock_);
    nl_socket_disable_auto_ack(this->sock_);

    if (genl_connect(this->sock_))
    {
        fprintf(stderr, "error on genl_connect\n");
        return -1;
    }

    family_id = genl_ctrl_resolve(this->sock_, VFSMONITOR_FAMILY_NAME);
    if (family_id < 0)
    {
        fprintf(stderr, "error on genl_ctrl_resolve\n: %d", family_id);
        return -1;
    }

    // 寻找广播地址
    int grp_id = genl_ctrl_resolve_grp(this->sock_, VFSMONITOR_FAMILY_NAME, VFSMONITOR_MCG_DENTRY_NAME);
    if (grp_id < 0)
    {
        fprintf(stderr, "error on genl_ctrl_resolve_group\n");
        return -1;
    }
    // 加入广播
    if (nl_socket_add_membership(this->sock_, grp_id))
    {
        fprintf(stderr, "error on join member ship");
        return -1;
    }

    // 设置广播接收
    nl_cb_set(cb_, NL_CB_VALID, NL_CB_CUSTOM, handleMsgFromGenl, this);

    /* init policy */
    vfsnotify_genl_policy[VFSMONITOR_A_ACT].type = NLA_U8;
    vfsnotify_genl_policy[VFSMONITOR_A_COOKIE].type = NLA_U32;
    vfsnotify_genl_policy[VFSMONITOR_A_MAJOR].type = NLA_U16;
    vfsnotify_genl_policy[VFSMONITOR_A_MINOR].type = NLA_U8;
    vfsnotify_genl_policy[VFSMONITOR_A_PATH].type = NLA_NUL_STRING;
    vfsnotify_genl_policy[VFSMONITOR_A_PATH].maxlen = 4096;
    // 开始监听
    start();
    return 0;
}


void DAGenlClient::run() {
    if (sock_ == NULL || cb_ == NULL) {
        qErrnoWarning("Nl Socket not opened.");
        return;
    }
    qDebug() << "start receiving msgs";
    int ret; 
    while ((ret = nl_recvmsgs(sock_, cb_)))
    {
        qDebug() << "received one msg";
        // ignore
    }
}
