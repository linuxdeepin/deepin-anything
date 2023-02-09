// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "eventsource_genl.h"
#include "vfs_change_consts.h"
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include "vfs_genl.h"
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QDebug>
#include <errno.h>
#include <QString>
#include <QFile>

DAS_BEGIN_NAMESPACE

#define OnError(message) do { qCritical() << message << QString::fromLocal8Bit(strerror(errno)); qApp->exit(errno); return false; } while(false)

Q_LOGGING_CATEGORY(genl, "genl", QtInfoMsg)
#define genlInfo(...) qCInfo(genl, __VA_ARGS__)

/* attribute policy */
static struct nla_policy vfsnotify_genl_policy[VFSMONITOR_A_MAX + 1];

static int add_group(struct nl_sock *nlsock, const char *group)
{
    int grp_id = genl_ctrl_resolve_grp(nlsock, VFSMONITOR_FAMILY_NAME, group);
    if(grp_id < 0) {
        genlInfo("genl_ctrl_resolve_grp fail\n");
        return 1;
    }
    if (nl_socket_add_membership(nlsock, grp_id)) {
        genlInfo("nl_socket_add_membership fail\n");
        return 1;
    }

    return 0;
}

EventSource_GENL::EventSource_GENL()
{
    inited = false;

    nlsock = nullptr;
    cb = nl_cb_alloc(NL_CB_DEFAULT);
    updatePartitions();

    buf[0] = 0;
    new_msg = false;
    act = (unsigned char)-1;
    dst = NULL;
}

EventSource_GENL::~EventSource_GENL()
{
    if (cb)
        nl_cb_put(cb);

    if (nlsock)
        nl_socket_free(nlsock);
}

bool EventSource_GENL::init()
{
    int family_id;

    if (inited)
        return true;

    nlsock = nl_socket_alloc();
    if (!nlsock) {
        genlInfo("nl_socket_alloc fail\n");
        return false;
    }

    nl_socket_disable_seq_check(nlsock);
    nl_socket_disable_auto_ack(nlsock);

    /* connect to genl */
    if (genl_connect(nlsock)) {
        genlInfo("genl_connect fail\n");
        goto exit_err;
    }

    /* resolve the generic nl family id*/
    family_id = genl_ctrl_resolve(nlsock, VFSMONITOR_FAMILY_NAME);
    if(family_id < 0) {
        genlInfo("genl_ctrl_resolve fail\n");
        goto exit_err;
    }

    /* add group */
    if (add_group(nlsock, VFSMONITOR_MCG_DENTRY_NAME))
        goto exit_err;

    /* set msg handler */
    nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, handleMsg, this);

    /* init policy */
    vfsnotify_genl_policy[VFSMONITOR_A_ACT].type = NLA_U8;
    vfsnotify_genl_policy[VFSMONITOR_A_COOKIE].type = NLA_U32;
    vfsnotify_genl_policy[VFSMONITOR_A_MAJOR].type = NLA_U16;
    vfsnotify_genl_policy[VFSMONITOR_A_MINOR].type = NLA_U8;
    vfsnotify_genl_policy[VFSMONITOR_A_PATH].type = NLA_NUL_STRING;
    vfsnotify_genl_policy[VFSMONITOR_A_PATH].maxlen = 4096;

    inited = true;
    return true;

exit_err:
    nl_socket_free(nlsock);
    nlsock = nullptr;
    return false;
}

bool EventSource_GENL::isInited()
{
    return inited;
}

bool EventSource_GENL::getEvent(unsigned char *type, char **src, char **dst, bool *end)
{
    nl_recvmsgs(nlsock, cb);

    if (new_msg) {

        *type = act;
        *src = buf;
        *dst = this->dst;
        *end = true;

        return true;
    } else {
        return false;
    }
}

#define MKDEV(ma,mi)    ((ma)<<8 | (mi))


void write_vfs_unnamed_device(const char *str)
{
    QString path("/sys/kernel/vfs_monitor/vfs_unnamed_devices");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QByteArray ba = path.toLatin1();
        genlInfo("open file failed, %s", ba.data());
        return;
    }
    file.write(str, strlen(str));
    file.close();
}

void read_vfs_unnamed_device(QSet<QByteArray> &devices)
{
    QString path("/sys/kernel/vfs_monitor/vfs_unnamed_devices");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QByteArray ba = path.toLatin1();
        genlInfo("open file failed, %s", ba.data());
        return;
    }
    QByteArray line = file.readLine();
    file.close();
    
    /* remove last \n */
    line.chop(1);
    QList<QByteArray> list = line.split(',');
    foreach (const QByteArray &minor, list) {
        devices.insert(minor);
    }
}

void update_vfs_unnamed_device(const QSet<QByteArray> &news)
{
    char buf[32];
    QSet<QByteArray> olds;
    read_vfs_unnamed_device(olds);

    QSet<QByteArray> removes(olds);
    removes.subtract(news);
    foreach (const QByteArray &minor, removes) {
        snprintf(buf, sizeof(buf), "r%s", minor.data());
        write_vfs_unnamed_device(buf);
    }

    QSet<QByteArray> adds(news);
    adds.subtract(olds);
    foreach (const QByteArray &minor, adds) {
        snprintf(buf, sizeof(buf), "a%s", minor.data());
        write_vfs_unnamed_device(buf);
    }
}

void EventSource_GENL::updatePartitions()
{
    QString file_mountinfo_path("/proc/self/mountinfo");
    QFile file_mountinfo(file_mountinfo_path);
    if (!file_mountinfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray ba = file_mountinfo_path.toLatin1();
        genlInfo("open file failed, %s", ba.data());
        return;
    }
    QByteArray mount_info;
    mount_info = file_mountinfo.readAll();
    file_mountinfo.close();

    unsigned int major, minor;
    char mp[256], root[256], type[256], *line = mount_info.data();
    QSet<QByteArray> dlnfs_devs;
    QByteArray ba;
    partitions.clear();
    genlInfo("updatePartitions start");
    while (sscanf(line, "%*d %*d %u:%u %250s %250s %*s %*s %*s %250s %*s %*s\n", &major, &minor, root, mp, type) == 5) {
        line = strchr(line, '\n') + 1;

        if (!major && strcmp(type, "fuse.dlnfs"))
            continue;

        if (!strcmp(root, "/")) {
            partitions.insert(MKDEV(major, minor), QByteArray(mp));
            genlInfo("%u:%u, %s", major, minor, mp);
            /* add monitoring for dlnfs device */
            if (!major && !strcmp(type, "fuse.dlnfs")) {
                ba.setNum(minor);
                dlnfs_devs.insert(ba);
            }
        }
    }
    update_vfs_unnamed_device(dlnfs_devs);
    genlInfo("updatePartitions end");
}

int EventSource_GENL::handleMsg(struct nl_msg *msg, void* arg)
{
    EventSource_GENL *event_src = static_cast<EventSource_GENL*>(arg);
    return event_src->handleMsg(msg);
}

#define get_attr(attrs, ATTR, attr, type) \
    if (!attrs[ATTR]) { \
        genlInfo("msg error: no " #ATTR "\n"); \
        return 0; \
    } \
    attr = nla_get_##type(attrs[ATTR])

int EventSource_GENL::handleMsg(struct nl_msg *msg)
{
    new_msg = false;
    struct nlattr *attrs[VFSMONITOR_A_MAX+1];
    int ret = genlmsg_parse(nlmsg_hdr(msg), 0, attrs, VFSMONITOR_A_MAX, vfsnotify_genl_policy);
    if (ret < 0) {
        genlInfo("print_msg fail: %d\n", ret);
        return 0;
    }

    unsigned char _act;
    char *_root, *_src, *_dst;
    unsigned int _cookie;
    unsigned short major = 0;
    unsigned char minor = 0;

    get_attr(attrs, VFSMONITOR_A_ACT, _act, u8);
    get_attr(attrs, VFSMONITOR_A_COOKIE, _cookie, u32);
    get_attr(attrs, VFSMONITOR_A_MAJOR, major, u16);
    get_attr(attrs, VFSMONITOR_A_MINOR, minor, u8);
    get_attr(attrs, VFSMONITOR_A_PATH, _src, string);

    if (_act < ACT_MOUNT) {
        if (!partitions.contains(MKDEV(major, minor))) {
            genlInfo("unknown device, %u, dev: %u:%u, path: %s, cookie: %u\n", _act, major, minor, _src, _cookie);
            return 0;
        }
        _root = partitions[MKDEV(major, minor)].data();
        if (strcmp(_root, "/") == 0)
            _root = nullptr;
    }

    switch(_act) {
    case ACT_NEW_FILE:
    case ACT_NEW_SYMLINK:
    case ACT_NEW_LINK:
    case ACT_NEW_FOLDER:
    case ACT_DEL_FILE:
    case ACT_DEL_FOLDER:
        _dst = nullptr;
        break;
    case ACT_RENAME_FROM_FILE:
    case ACT_RENAME_FROM_FOLDER:
        rename_from.insert(_cookie, QByteArray(_src));
        return 0;
    case ACT_RENAME_TO_FILE:
    case ACT_RENAME_TO_FOLDER:
        if (rename_from.contains(_cookie)) {
            _act = _act == ACT_RENAME_TO_FILE ? ACT_RENAME_FILE : ACT_RENAME_FOLDER;
            _dst = _src;
            _src = rename_from[_cookie].data();
        }
        break;
    case ACT_MOUNT:
    case ACT_UNMOUNT:
        updatePartitions();
        return 0;
    case ACT_RENAME_FILE:
    case ACT_RENAME_FOLDER:
        genlInfo("not support file action: %d\n", int(_act));
        return 0;
    default:
        genlInfo("Unknow file action: %d\n", int(_act));
        return 0;
    }

    if (saveData(_act, _root, _src, _dst)) {
        new_msg = true;
    }

    if (_act == ACT_RENAME_FILE || _act == ACT_RENAME_FOLDER)
        rename_from.remove(_cookie);

    return 0;
}

bool EventSource_GENL::saveData(unsigned char _act, char *_root, char *_src, char *_dst)
{
    size_t root_size = _root ? strlen(_root) : 0;
    size_t src_size = strlen(_src);

    if (_dst) {
        size_t dst_size = strlen(_dst);
        if (root_size*2+src_size+dst_size+2 > sizeof(buf)) {
            genlInfo("the msg buf is too small to cache msg\n");
            return false;
        }
    } else {
        if (root_size+src_size+1 > sizeof(buf)) {
            genlInfo("the msg buf is too small to cache msg\n");
            return false;
        }
    }

    act = _act;

    /* save src */
    if (_root)
        strcpy(buf, _root);
    strcpy(buf+root_size, _src);

    /* save dst */
    if (_dst) {
        dst = buf + root_size + src_size + 1;
        if (_root)
            strcpy(dst, _root);
        strcpy(dst+root_size, _dst);
    } else {
        dst = nullptr;
    }

    return true;
}

DAS_END_NAMESPACE
