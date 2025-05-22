// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/event_listenser.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h> // close()

#include <memory> // unique_ptr
#include <unordered_map>
#include <sstream>

#include <netlink/genl/ctrl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/genl.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <QCoreApplication>

#include "utils/genl_parser.hpp"
#include "utils/log.h"
#include "vfs_change_consts.h"

#include <gmodule.h>

ANYTHING_NAMESPACE_BEGIN

constexpr int epoll_size = 10;
static nla_policy vfs_policy[VFSMONITOR_A_MAX + 1];

event_listenser::event_listenser()
    : connected_{ connect(mcsk_) },
      timeout_{ -1 } {
    auto clean_and_exit = [this] {
        disconnect(mcsk_);
        exit(1);
    };

    if (!connected_) {
        spdlog::error("Error: failed to connect to generic netlink");
        clean_and_exit();
    }

    // Disable sequence checks for asynchronous multicast messages
    nl_socket_disable_seq_check(mcsk_);
    nl_socket_disable_auto_ack(mcsk_);

    // Resolve the multicast group
    int mcgrp = genl_ctrl_resolve_grp(mcsk_, VFSMONITOR_FAMILY_NAME, VFSMONITOR_MCG_DENTRY_NAME);
    if (mcgrp < 0) {
		spdlog::error("Error: failed to resolve generic netlink multicast group");
		clean_and_exit();
	}

    // Joint the multicast group
    int ret = nl_socket_add_membership(mcsk_, mcgrp);
    if (ret < 0) {
        spdlog::error("Error: failed to join multicast group");
        clean_and_exit();
    }

    if (!set_callback(mcsk_, event_listenser::event_handler)) {
        spdlog::error("Error: failed to set callback");
        clean_and_exit();
    }

    stop_fd_ = eventfd(0, EFD_NONBLOCK);
    if (stop_fd_ == -1) {
        spdlog::error("Failed to create eventfd");
        clean_and_exit();
    }

    // Initialize policy
    vfs_policy[VFSMONITOR_A_ACT].type = NLA_U8;
    vfs_policy[VFSMONITOR_A_COOKIE].type = NLA_U32;
    vfs_policy[VFSMONITOR_A_MAJOR].type = NLA_U16;
    vfs_policy[VFSMONITOR_A_MINOR].type = NLA_U8;
    vfs_policy[VFSMONITOR_A_PATH].type = NLA_NUL_STRING;
    vfs_policy[VFSMONITOR_A_PATH].maxlen = 4096;
}

event_listenser::~event_listenser() {
    disconnect(mcsk_);
    close(stop_fd_);
}

void event_listenser::start_listening() {
    spdlog::info("listening for messages");
    int ep_fd = epoll_create1(0);
    if (ep_fd < 0) {
        spdlog::error("Epoll creation failed.");
        return;
    }

    int mcsk_fd = get_fd(mcsk_);
    epoll_event* ep_events = new epoll_event[epoll_size];
    epoll_event event[2];
    event[0].events = EPOLLIN;
    event[0].data.fd = mcsk_fd;
    event[1].events = EPOLLIN;
    event[1].data.fd = stop_fd_;
    epoll_ctl(ep_fd, EPOLL_CTL_ADD, mcsk_fd, &event[0]);
    epoll_ctl(ep_fd, EPOLL_CTL_ADD, stop_fd_, &event[1]);

    bool running = true;
    while (running) {
        int event_cnt = epoll_wait(ep_fd, ep_events, epoll_size, timeout_);
        if (event_cnt == -1) {
            if (errno == EINTR) {
                continue;
            }
            spdlog::error("epoll_wait() error: {} (errno: {})", strerror(errno), errno);
            break;
        }

        for (int i = 0; i < event_cnt; ++i) {
            if (ep_events[i].data.fd == mcsk_fd) {
                int ret = nl_recvmsgs_default(mcsk_);
                if (ret < 0) {
                    spdlog::error("Failed to receive netlink messages: {}", ret);
                    // not exit, continue listening
                }
            } else if (ep_events[i].data.fd == stop_fd_) {
                uint64_t u;
                [[maybe_unused]] auto _ = read(stop_fd_, &u, sizeof(u));
                running = false;
                break;
            }
        }
    }

    close(ep_fd);
    delete[] ep_events;
}

void event_listenser::async_listen() {
    listening_thread_ = std::thread(&event_listenser::start_listening, this);
}

void event_listenser::stop_listening() {
    uint64_t u = 1;
    [[maybe_unused]] auto _ = write(stop_fd_, &u, sizeof(u));

    if (listening_thread_.joinable()) {
        auto thread_id = listening_thread_.get_id();
        listening_thread_.join();
        std::ostringstream oss;
        oss << thread_id;
        spdlog::info("Listening thread {} has exited.", oss.str());
    }
}

void event_listenser::set_handler(std::function<void(fs_event*)> handler) {
    handler_ = std::move(handler);
}

bool event_listenser::connect(nl_sock_ptr& sk) const {
    sk = nl_socket_alloc();
    return sk ? genl_connect(sk) == 0 : false;
}

void event_listenser::disconnect(nl_sock_ptr& sk) const {
    nl_socket_free(sk);
}

bool event_listenser::set_callback(nl_sock_ptr& sk, nl_recvmsg_msg_cb_t func) {
    return nl_socket_modify_cb(sk, NL_CB_VALID, NL_CB_CUSTOM, func, this) == 0;
}

int event_listenser::get_fd(nl_sock_ptr& sk) const {
    return nl_socket_get_fd(sk);
}

void event_listenser::forward_event_to_handler(fs_event *event) const {
    if (handler_) {
        std::invoke(handler_, event);
    }
}

fs_event* make_fs_event(uint8_t act,
                        uint32_t cookie,
                        uint16_t major,
                        uint8_t minor,
                        const char* src,
                        const char* dst) {
    fs_event* event = g_slice_new(fs_event);
    event->act = act;
    event->cookie = cookie;
    event->major = major;
    event->minor = minor;
    strncpy(event->src, src, MAX_PATH_LEN);
    event->src[MAX_PATH_LEN - 1] = '\0';
    strncpy(event->dst, dst, MAX_PATH_LEN);
    event->dst[MAX_PATH_LEN - 1] = '\0';
    return event;
}

int event_listenser::event_handler(nl_msg_ptr msg, void* arg) {
    nlattr* tb[VFSMONITOR_A_MAX + 1];
    int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, VFSMONITOR_A_MAX, vfs_policy);
    if (err < 0) {
        spdlog::error("Unable to parse the message: {}", strerror(-err));
        return NL_SKIP;
    }

    if (!tb[VFSMONITOR_A_PATH]) {
        spdlog::error("Attributes missing from the message");
        return NL_SKIP;
    }

    nla_parser parser(tb);
    auto act    = parser.get_value<nla_u8>(VFSMONITOR_A_ACT);
    auto cookie = parser.get_value<nla_u32>(VFSMONITOR_A_COOKIE);
    auto major  = parser.get_value<nla_u16>(VFSMONITOR_A_MAJOR);
    auto minor  = parser.get_value<nla_u8>(VFSMONITOR_A_MINOR);
    auto src    = parser.get_value<nla_string>(VFSMONITOR_A_PATH);
    if (!act || !cookie || !major || !minor || !src) {
        spdlog::error("Attributes missing from the message");
        return NL_SKIP;
    }

    fs_event* event = make_fs_event(*act, *cookie, *major, *minor, *src, "");
    auto listenser = static_cast<event_listenser*>(arg);
    listenser->forward_event_to_handler(event);
    return NL_OK;
}

ANYTHING_NAMESPACE_END