// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <pthread.h>
#include <stdio.h>

#include "vfsgenl.h"

#define get_attr(attrs, ATTR, attr, type) \
  if (!attrs[ATTR])                       \
  {                                       \
    return 0;                             \
  }                                       \
  attr = nla_get_##type(attrs[ATTR])

/* attribute policy */
static struct nla_policy vfsnotify_genl_policy[VFSMONITOR_A_MAX + 1];

static int handle_msg_from_deepin_anything(struct nl_msg *msg, void *arg)
{
  printf("recved one msg from deepin-anything genl\n");
  struct nlattr *attrs[VFSMONITOR_A_MAX + 1];
  int ret = genlmsg_parse(nlmsg_hdr(msg), 0, attrs, VFSMONITOR_A_MAX,
                          vfsnotify_genl_policy);
  if (ret < 0)
  {
    printf("error parse genl msg\n");
    return 1;
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
  return 0;
}

struct nl_cb *prepare_sock(struct nl_sock *nl_sock)
{
  struct nl_cb *nl_cb;
  int family_id;

  nl_cb = nl_cb_alloc(NL_CB_DEFAULT);
  if (!nl_cb)
  {
    printf("error on nl_cb_alloc\n");
    return NULL;
  }

  nl_socket_disable_seq_check(nl_sock);
  nl_socket_disable_auto_ack(nl_sock);

  if (genl_connect(nl_sock))
  {
    printf("error on genl_connect\n");
    return NULL;
  }

  family_id = genl_ctrl_resolve(nl_sock, VFSMONITOR_FAMILY_NAME);
  if (family_id < 0)
  {
    printf("error on genl_ctrl_resolve\n: %d", family_id);
    return NULL;
  }

  // 寻找广播地址
  int grp_id = genl_ctrl_resolve_grp(nl_sock, VFSMONITOR_FAMILY_NAME,
                                     VFSMONITOR_MCG_DENTRY_NAME);
  if (grp_id < 0)
  {
    printf("error on genl_ctrl_resolve_group\n");
    return NULL;
  }
  // 加入广播
  if (nl_socket_add_membership(nl_sock, grp_id))
  {
    printf("error on join member ship");
    return NULL;
  }

  // 设置广播接收
  nl_cb_set(nl_cb, NL_CB_VALID, NL_CB_CUSTOM, handle_msg_from_deepin_anything,
            NULL);

  /* init policy */
  vfsnotify_genl_policy[VFSMONITOR_A_ACT].type = NLA_U8;
  vfsnotify_genl_policy[VFSMONITOR_A_COOKIE].type = NLA_U32;
  vfsnotify_genl_policy[VFSMONITOR_A_MAJOR].type = NLA_U16;
  vfsnotify_genl_policy[VFSMONITOR_A_MINOR].type = NLA_U8;
  vfsnotify_genl_policy[VFSMONITOR_A_PATH].type = NLA_NUL_STRING;
  vfsnotify_genl_policy[VFSMONITOR_A_PATH].maxlen = 4096;

  return nl_cb;
}

int main(int argc, char const *argv[])
{
  struct nl_sock *nl_sock;
  struct nl_cb *nl_cb;
  int family_id;
  int ret;

  nl_sock = nl_socket_alloc();
  if (!nl_sock)
  {
    goto exit_err;
  }
  nl_cb = prepare_sock(nl_sock);
  if (!nl_cb)
  {
    goto exit_err;
  }
  printf("listening\n");
  ret = nl_recvmsgs(nl_sock, nl_cb);
  while (!ret)
  {
    ret = nl_recvmsgs(nl_sock, nl_cb);
    // block
  }
  printf("recv msg returned %d", ret);
exit_err:
  nl_cb_put(nl_cb);
  nl_socket_free(nl_sock);
  return 0;
}
