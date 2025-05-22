// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_EVENT_LISTENSER_H_
#define ANYTHING_EVENT_LISTENSER_H_

#include <atomic>
#include <functional>
#include <thread>

#include <netlink/attr.h>
#include <netlink/handlers.h>

#include "common/anything_fwd.hpp"
#include "common/fs_event.h"
#include "common/vfs_genl.h"

ANYTHING_NAMESPACE_BEGIN

using nl_sock_ptr = nl_sock*;
using nl_msg_ptr  = nl_msg*;

class event_listenser {
public:
    event_listenser();
    ~event_listenser();

    void start_listening();

    void async_listen();

    void stop_listening();

    void set_handler(std::function<void(fs_event*)> handler);

private:
    bool connect(nl_sock_ptr& sk) const;
    void disconnect(nl_sock_ptr& sk) const;
    bool set_callback(nl_sock_ptr& sk, nl_recvmsg_msg_cb_t func);
    int get_fd(nl_sock_ptr& sk) const;

    void forward_event_to_handler(fs_event *event) const;

    static int event_handler(nl_msg_ptr msg, void* arg);

private:
    nl_sock_ptr mcsk_;
    bool connected_;
    int stop_fd_;
    int timeout_;
    std::function<void(fs_event*)> handler_;
    std::thread listening_thread_;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_EVENT_LISTENSER_H_