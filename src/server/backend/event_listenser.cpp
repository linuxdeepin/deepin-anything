#include "event_listenser.h"

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

#include "genl_parser.hpp"
#include "vfs_change_consts.h"

#define EPOLL_SIZE         10
#define FS_EVENT_MAX_BATCH 1


namespace anything {

static nla_policy vfs_policy[VFSMONITOR_A_MAX + 1];

event_listenser::event_listenser()
    : connected_{ connect(mcsk_) }
{
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

    // Scan the partitions
    // if (!partitions_.update()) {
    //     std::cerr << "Ensure you are running in root mode and have loaded the vfs_monitor module (use: sudo modprobe vfs_monitor).\n";
    //     clean_and_abort();
    // }

    // Initialize policy
    vfs_policy[VFSMONITOR_A_ACT].type = NLA_U8;
    vfs_policy[VFSMONITOR_A_COOKIE].type = NLA_U32;
    vfs_policy[VFSMONITOR_A_MAJOR].type = NLA_U16;
    vfs_policy[VFSMONITOR_A_MINOR].type = NLA_U8;
    vfs_policy[VFSMONITOR_A_PATH].type = NLA_NUL_STRING;
    vfs_policy[VFSMONITOR_A_PATH].maxlen = 4096;
}

event_listenser::~event_listenser()
{
    disconnect(mcsk_);
    // pool_.wait_for_tasks();
}

void event_listenser::start_listening()
{
    should_stop_ = false;
    printf("listening for messages\n");
    int ep_fd = epoll_create1(0);
    if (ep_fd < 0) {
        std::cerr << "Epoll creation failed.\n";
        return;
    }

    int mcsk_fd = get_fd(mcsk_);
    epoll_event* ep_events = new epoll_event[EPOLL_SIZE];
    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = mcsk_fd;
    epoll_ctl(ep_fd, EPOLL_CTL_ADD, mcsk_fd, &event);

    int timeout = -1;
    while (!should_stop_) {
        int event_cnt = epoll_wait(ep_fd, ep_events, EPOLL_SIZE, timeout);
        if (event_cnt == -1) {
            if (errno == EINTR)
                break;
            std::cerr << "epoll_wait() error\n";
            break;
        }

        // if (event_cnt == 0) {
        //     if (!fsevents_.empty()) {
        //         std::lock_guard lock(fsevent_mtx_);
        //         if (!fsevents_.empty()) {
        //             // std::cout << "The fs events needed to handle: " << fsevents_.size() << "\n";
        //             cv_.notify_one();
        //         }
        //     }
        //     continue;
        // }

        for (int i = 0; i < event_cnt; ++i) {
            if (ep_events[i].data.fd == mcsk_fd) {
                // std::cout << "received event on mcsk\n";
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
    handler_ = handler;
}

bool event_listenser::connect(nl_sock_ptr& sk)
{
    sk = nl_socket_alloc();
    return sk ? genl_connect(sk) == 0 : false;
}

void event_listenser::disconnect(nl_sock_ptr& sk)
{
    nl_socket_free(sk);
}

bool event_listenser::set_callback(nl_sock_ptr& sk, nl_recvmsg_msg_cb_t func)
{
    return nl_socket_modify_cb(sk, NL_CB_VALID, NL_CB_CUSTOM, func, this) == 0;
}

int event_listenser::get_fd(nl_sock_ptr& sk) const
{
    return nl_socket_get_fd(sk);
}

void event_listenser::forward_event_to_handler(fs_event event)
{
    if (handler_)
        std::invoke(handler_, std::move(event));
}

int event_listenser::event_handler(nl_msg_ptr msg, void* arg)
{
    static std::unordered_map<uint32_t, std::string> rename_from;

    nlattr* tb[VFSMONITOR_A_MAX + 1];
    int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, VFSMONITOR_A_MAX, vfs_policy);
    if (err < 0) {
        std::cerr << "unable to parse message: " << strerror(-err) << "\n";
        return NL_SKIP;
    }

    if (!tb[VFSMONITOR_A_PATH]) {
        std::cerr << "msg attribute missing from message\n";
		return NL_SKIP;
    }

    nla_parser parser(tb);
    auto act    = parser.get_value<nla_u8>(VFSMONITOR_A_ACT);
    auto cookie = parser.get_value<nla_u32>(VFSMONITOR_A_COOKIE);
    auto major  = parser.get_value<nla_u16>(VFSMONITOR_A_MAJOR);
    auto minor  = parser.get_value<nla_u8>(VFSMONITOR_A_MINOR);
    auto src    = parser.get_value<nla_string>(VFSMONITOR_A_PATH);
    if (!act || !cookie || !major || !minor || !src) {
        std::cerr << "msg attribute missing from message\n";
        return NL_SKIP;
    }

    fs_event event{*act, *cookie, *major, *minor, std::string(*src), ""};
    auto listenser = static_cast<event_listenser*>(arg);
    listenser->forward_event_to_handler(std::move(event));
    return NL_OK;
}

} // namespace anything