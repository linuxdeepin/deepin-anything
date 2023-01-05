// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dagenlclient.h"

#include <libnl3/netlink/netlink.h>

#include <QDebug>

#include "vfsevent.h"
#include "vfsgenl.h"

#define get_attr(attrs, ATTR, attr, type) \
  if (!attrs[ATTR]) {                     \
    return 0;                             \
  }                                       \
  attr = nla_get_##type(attrs[ATTR])

/* major, minor*/
#define MKDEV(ma, mi) ((ma) << 8 | (mi))

/* attribute policy */
static struct nla_policy vfsnotify_genl_policy[VFSMONITOR_A_MAX + 1];

DAGenlClient::DAGenlClient() : QThread(nullptr){};

DAGenlClient::~DAGenlClient() {
  qDebug() << "stopping genl client";
  if (cb_) {
    nl_cb_put(cb_);
    cb_ = nullptr;
  }
  if (sock_) {
    nl_socket_free(sock_);
    sock_ = nullptr;
  }
}

int DAGenlClient::handleMsgFromGenl(struct nl_msg *msg, void *arg) {
  DAGenlClient *client = (DAGenlClient *)arg;
  return client->handleMsg(msg);
}

int DAGenlClient::handleMsg(struct nl_msg *msg) {
  struct nlattr *attrs[VFSMONITOR_A_MAX + 1];
  unsigned char act;
  char *src = nullptr, *dst = nullptr;
  unsigned int cookie;
  unsigned short major = 0;
  unsigned char minor = 0;
  int ret = genlmsg_parse(nlmsg_hdr(msg), 0, attrs, VFSMONITOR_A_MAX,
                          vfsnotify_genl_policy);
  if (ret < 0) {
    printf("error parse genl msg\n");
    return 1;
  }

  get_attr(attrs, VFSMONITOR_A_ACT, act, u8);
  get_attr(attrs, VFSMONITOR_A_COOKIE, cookie, u32);
  get_attr(attrs, VFSMONITOR_A_MAJOR, major, u16);
  get_attr(attrs, VFSMONITOR_A_MINOR, minor, u8);
  get_attr(attrs, VFSMONITOR_A_PATH, src, string);

  // parse msg
  switch (act) {
    case ACT_NEW_FILE:
    case ACT_NEW_SYMLINK:
    case ACT_NEW_LINK:
    case ACT_NEW_FOLDER:
    case ACT_DEL_FILE:
    case ACT_DEL_FOLDER:
      dst = nullptr;
      break;
    case ACT_RENAME_FROM_FILE:
    case ACT_RENAME_FROM_FOLDER:
      rename_from_.insert(cookie, QByteArray(src));
      return 0;
    case ACT_RENAME_TO_FILE:
    case ACT_RENAME_TO_FOLDER:
      if (rename_from_.contains(cookie)) {
        act = act == ACT_RENAME_TO_FILE ? ACT_RENAME_FILE : ACT_RENAME_FOLDER;
        dst = src;
        src = rename_from_[cookie].data();
      }
      break;
    case ACT_MOUNT:
    case ACT_UNMOUNT:
      emit onPartitionUpdate();
      return 0;
    case ACT_RENAME_FILE:
    case ACT_RENAME_FOLDER:
      qDebug() << "not support file action" << act;
      return 0;
    default:
      qDebug() << "Unknow file action" << act;
      return 0;
  }

  // send signal
  VfsEvent evt{};
  evt.act_ = act;
  evt.major_ = major;
  evt.minor_ = minor;
  evt.src_ = src;
  evt.dst_ = dst == nullptr ? "" : dst;
  emit onVfsEvent(evt);
  if (act == ACT_RENAME_FILE || act == ACT_RENAME_FOLDER) {
    rename_from_.remove(cookie);
  }
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
  if (!cb_) {
    fprintf(stderr, "error on nl_cb_alloc\n");
    return -1;
  }

  nl_socket_disable_seq_check(this->sock_);
  nl_socket_disable_auto_ack(this->sock_);

  if (genl_connect(this->sock_)) {
    fprintf(stderr, "error on genl_connect\n");
    return -1;
  }

  family_id = genl_ctrl_resolve(this->sock_, VFSMONITOR_FAMILY_NAME);
  if (family_id < 0) {
    fprintf(stderr, "error on genl_ctrl_resolve\n: %d", family_id);
    return -1;
  }

  // 寻找广播地址
  int grp_id = genl_ctrl_resolve_grp(this->sock_, VFSMONITOR_FAMILY_NAME,
                                     VFSMONITOR_MCG_DENTRY_NAME);
  if (grp_id < 0) {
    fprintf(stderr, "error on genl_ctrl_resolve_group\n");
    return -1;
  }
  // 加入广播
  if (nl_socket_add_membership(this->sock_, grp_id)) {
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
  int ret = nl_recvmsgs(sock_, cb_);
  while (!ret) {
    qDebug() << "received one msg";
    // ignore
    ret = nl_recvmsgs(sock_, cb_);
  }
  qErrnoWarning("DAgenlClient OFFLINE!");
}
