#include "anything/core/event_listenser.h"

#include <sys/epoll.h>
#include <unistd.h> // close()

#include <memory> // unique_ptr
#include <iostream>
#include <unordered_map>

#include <netlink/genl/ctrl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/genl.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>

#include "anything/utils/genl_parser.hpp"
#include "anything/utils/log.h"
#include "vfs_change_consts.h"

#define EPOLL_SIZE         10
#define FS_EVENT_MAX_BATCH 1

ANYTHING_NAMESPACE_BEGIN

static nla_policy vfs_policy[VFSMONITOR_A_MAX + 1];

event_listenser::event_listenser()
    : connected_{ connect(mcsk_) },
      timeout_{ -1 } {
    auto clean_and_abort = [this] {
        disconnect(mcsk_);
        std::abort();
    };

    if (!connected_) {
        std::cerr << "Error: failed to connect to generic netlink\n";
        clean_and_abort();
    }

    // Disable sequence checks for asynchronous multicast messages
    nl_socket_disable_seq_check(mcsk_);
    nl_socket_disable_auto_ack(mcsk_);

    // Resolve the multicast group
    int mcgrp = genl_ctrl_resolve_grp(mcsk_, VFSMONITOR_FAMILY_NAME, VFSMONITOR_MCG_DENTRY_NAME);
    if (mcgrp < 0) {
		std::cerr << "Error: failed to resolve generic netlink multicast group\n";
		clean_and_abort();
	}

    // Joint the multicast group
    int ret = nl_socket_add_membership(mcsk_, mcgrp);
    if (ret < 0) {
        std::cerr << "Error: failed to join multicast group\n";
        clean_and_abort();
    }

    if (!set_callback(mcsk_, event_listenser::event_handler)) {
        std::cerr << "Error: failed to set callback\n";
        clean_and_abort();
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
}

void event_listenser::start_listening() {
    should_stop_ = false;
    log::info("listening for messages");
    int ep_fd = epoll_create1(0);
    if (ep_fd < 0) {
        log::error("Epoll creation failed.");
        return;
    }

    int mcsk_fd = get_fd(mcsk_);
    epoll_event* ep_events = new epoll_event[EPOLL_SIZE];
    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = mcsk_fd;
    epoll_ctl(ep_fd, EPOLL_CTL_ADD, mcsk_fd, &event);

    while (!should_stop_) {
        int event_cnt = epoll_wait(ep_fd, ep_events, EPOLL_SIZE, timeout_);
        if (event_cnt == -1) {
            if (errno == EINTR) break;
            log::error("epoll_wait() error");
            break;
        }

        // Process the idle task
        if (event_cnt == 0) {
            if (idle_task_) idle_task_();
            continue;
        }

        for (int i = 0; i < event_cnt; ++i) {
            if (ep_events[i].data.fd == mcsk_fd) {
                nl_recvmsgs_default(mcsk_);
            }
        }
    }

    close(ep_fd);
    delete[] ep_events;
}

void event_listenser::stop_listening() {
    should_stop_ = true;
}

void event_listenser::set_handler(std::function<void(fs_event)> handler) {
    handler_ = std::move(handler);
}

void event_listenser::set_idle_task(std::function<void()> idle_task, int timeout) {
    idle_task_ = std::move(idle_task);
    timeout_ = timeout;
}

bool event_listenser::connect(nl_sock_ptr& sk) {
    sk = nl_socket_alloc();
    return sk ? genl_connect(sk) == 0 : false;
}

void event_listenser::disconnect(nl_sock_ptr& sk) {
    nl_socket_free(sk);
}

bool event_listenser::set_callback(nl_sock_ptr& sk, nl_recvmsg_msg_cb_t func) {
    return nl_socket_modify_cb(sk, NL_CB_VALID, NL_CB_CUSTOM, func, this) == 0;
}

int event_listenser::get_fd(nl_sock_ptr& sk) const {
    return nl_socket_get_fd(sk);
}

void event_listenser::forward_event_to_handler(fs_event event) {
    if (handler_) {
        std::invoke(handler_, std::move(event));
    }
}

int event_listenser::event_handler(nl_msg_ptr msg, void* arg) {
    nlattr* tb[VFSMONITOR_A_MAX + 1];
    int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, VFSMONITOR_A_MAX, vfs_policy);
    if (err < 0) {
        log::error("Unable to parse the message: {}", strerror(-err));
        return NL_SKIP;
    }

    if (!tb[VFSMONITOR_A_PATH]) {
        log::error("Attributes missing from the message");
		return NL_SKIP;
    }

    nla_parser parser(tb);
    auto act    = parser.get_value<nla_u8>(VFSMONITOR_A_ACT);
    auto cookie = parser.get_value<nla_u32>(VFSMONITOR_A_COOKIE);
    auto major  = parser.get_value<nla_u16>(VFSMONITOR_A_MAJOR);
    auto minor  = parser.get_value<nla_u8>(VFSMONITOR_A_MINOR);
    auto src    = parser.get_value<nla_string>(VFSMONITOR_A_PATH);
    if (!act || !cookie || !major || !minor || !src) {
        log::error("Attributes missing from the message");
        return NL_SKIP;
    }

    fs_event event{*act, *cookie, *major, *minor, std::string(*src), ""};
    auto listenser = static_cast<event_listenser*>(arg);
    listenser->forward_event_to_handler(std::move(event));
    return NL_OK;
}

ANYTHING_NAMESPACE_END