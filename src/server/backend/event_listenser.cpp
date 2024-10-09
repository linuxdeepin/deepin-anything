#include "event_listenser.h"
#include "genl_parser.hpp"
#include "lftmanager.h"
#include "logdefine.h"
#include "vfs_change_consts.h"
#include <memory> // unique_ptr
#include <iostream>
#include <unordered_map>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/genl.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <sys/epoll.h>
#include <unistd.h> // close()
#define EPOLL_SIZE         10
#define FS_EVENT_MAX_BATCH 1


namespace anything {

Q_LOGGING_CATEGORY(logN, "anything.normal.genl", DEFAULT_MSG_TYPE)

static nla_policy vfs_policy[VFSMONITOR_A_MAX + 1];

event_listenser::event_listenser()
    : connected_{ connect(mcsk_) }
{
    auto clean_and_abort = [this] {
        disconnect(mcsk_);
        std::abort();
        // throw std::bad_alloc();
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
    if (!partitions_.update()) {
        std::cerr << "Ensure you are running in root mode and have loaded the vfs_monitor module (use: sudo modprobe vfs_monitor).\n";
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

event_listenser::~event_listenser()
{
    disconnect(mcsk_);
    pool_.wait_for_tasks();
}

void event_listenser::listen()
{
    printf("listening for messages\n");
    int ep_fd = epoll_create1(0);
    if (ep_fd < 0) {
        nWarning("Epoll creation failed.\n");
        return;
    }

    int mcsk_fd = get_fd(mcsk_);
    epoll_event* ep_events = new epoll_event[EPOLL_SIZE];
    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = mcsk_fd;
    epoll_ctl(ep_fd, EPOLL_CTL_ADD, mcsk_fd, &event);

    int timeout = -1;
    for (;;) {
        int event_cnt = epoll_wait(ep_fd, ep_events, EPOLL_SIZE, timeout);
        if (event_cnt == -1) {
            nWarning("epoll_wait() error\n");
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

void event_listenser::push_fs_event(fs_event event)
{
    pool_.enqueue_detach([this, event = std::move(event)]() {
        this->fs_event_handler(std::move(event));
    });
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

int event_listenser::event_handler(nl_msg_ptr msg, void* arg)
{
    static std::unordered_map<uint32_t, std::string> rename_from;

    nlattr* tb[VFSMONITOR_A_MAX + 1];
    int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, VFSMONITOR_A_MAX, vfs_policy);
    if (err < 0) {
        nWarning("unable to parse message: %s\n", strerror(-err));
        return NL_SKIP;
    }

    if (!tb[VFSMONITOR_A_PATH]) {
        nWarning("msg attribute missing from message\n");
		return NL_SKIP;
    }

    nla_parser parser(tb);
    auto act    = parser.get_value<nla_u8>(VFSMONITOR_A_ACT);
    auto cookie = parser.get_value<nla_u32>(VFSMONITOR_A_COOKIE);
    auto major  = parser.get_value<nla_u16>(VFSMONITOR_A_MAJOR);
    auto minor  = parser.get_value<nla_u8>(VFSMONITOR_A_MINOR);
    auto src    = parser.get_value<nla_string>(VFSMONITOR_A_PATH);
    if (!act || !cookie || !major || !minor || !src) {
        nWarning("msg attribute missing from message\n");
        return NL_SKIP;
    }

    ///////////////////////////
    fs_event event{*act, *cookie, *major, *minor, std::string(*src)};
    auto listenser = static_cast<event_listenser*>(arg);

    // Update partition event
    if (event.act == ACT_MOUNT || event.act == ACT_UNMOUNT) {
        listenser->push_fs_event(std::move(event));
        return NL_OK;
    }

    std::string root;
    if (event.act < ACT_MOUNT) {
        if (!listenser->partition_contains(MKDEV(event.major, event.minor))) {
            nWarning("unknown device, %u, dev: %u:%u, path: %s, cookie: %u.", event.act, event.major, event.minor, event.src, event.cookie);
            return NL_SKIP;
        }

        root = listenser->partition_get(MKDEV(event.major, event.minor));
        
        // std::cout << "root: " << root << "\n";
        if (root == "/")
            root.clear();
    }

    switch (event.act) {
    case ACT_NEW_FILE:
    case ACT_NEW_SYMLINK:
    case ACT_NEW_LINK:
    case ACT_NEW_FOLDER:
    case ACT_DEL_FILE:
    case ACT_DEL_FOLDER:
        // Keeps the dst empty.
        break;
    case ACT_RENAME_FROM_FILE:
    case ACT_RENAME_FROM_FOLDER:
        rename_from.emplace(event.cookie, event.src);
        return NL_SKIP;
    case ACT_RENAME_TO_FILE:
    case ACT_RENAME_TO_FOLDER:
        if (auto search = rename_from.find(event.cookie);
            search != rename_from.end()) {
            event.act = event.act == ACT_RENAME_TO_FILE ? ACT_RENAME_FILE : ACT_RENAME_FOLDER;
            event.dst = event.src;
            event.src = rename_from[event.cookie];
        }
        break;
    case ACT_RENAME_FILE:
    case ACT_RENAME_FOLDER:
        nWarning("Not support file action: %d.", +event.act);
        return NL_SKIP;
    default:
        nWarning("Unknow file action: %d.", +event.act);
        return NL_SKIP;
    }

    if (!root.empty()) {
        event.src = root + event.src;
        if (!event.dst.empty())
            event.dst = root + event.dst;
    }

    if (event.act == ACT_RENAME_FILE || event.act == ACT_RENAME_FOLDER) {
        rename_from.erase(event.cookie);
    }
    ///////////////////////////

    listenser->push_fs_event(std::move(event));
    return NL_OK;
}

void event_listenser::fs_event_handler(fs_event event)
{
    const char* act_names[] = {"file_created", "link_created", "symlink_created", "dir_created", "file_deleted", "dir_deleted", "file_renamed", "dir_renamed"};

    if (event.act == ACT_MOUNT || event.act == ACT_UNMOUNT) {
        std::lock_guard<std::mutex> lock(mtx_);
        partitions_.update();
        return;
    }

    // {
    //     std::lock_guard<std::mutex> lock(mtx_);
    //     std::cout << "message handling: [act:\"" << act_names[event.act] << "\", src:\"" << event.src
    //         << "\", dst:\"" << event.dst << "\"]\n";
    //     // nWarning("message handled: [act:\"%s\", src: \"%s\", dst: \"%s\"]\n",
    //     //     act_names[event.act], event.src.data(), event.dst.data());   
    // }
    
    // Preparations are done, starting to process the event.
    bool ignored = false;
    ignored = ignored_event(event.dst.empty() ? event.src : event.dst, ignored);
    if (!ignored) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (event.act == ACT_NEW_FILE || event.act == ACT_NEW_SYMLINK ||
            event.act == ACT_NEW_LINK || event.act == ACT_NEW_FOLDER) {
            LFTManager::instance()->insertFileToLFTBuf(QByteArray(event.src.c_str(), event.src.length()));
        } else if (event.act == ACT_DEL_FILE || event.act == ACT_DEL_FOLDER) {
            LFTManager::instance()->removeFileFromLFTBuf(QByteArray(event.src.c_str(), event.src.length()));
        } else if (event.act == ACT_RENAME_FILE || event.act == ACT_RENAME_FOLDER) {
            LFTManager::instance()->renameFileOfLFTBuf(QByteArray(event.src.c_str(), event.src.length()), 
                QByteArray(event.dst.c_str(), event.dst.length()));
        }
    }
}

bool event_listenser::ignored_event(const std::string& path, bool ignored)
{
    auto ends_with = [](const std::string& path, const std::string& ending) {
        if (path.length() >= ending.length())
            return 0 == path.compare(path.length() - ending.length(), ending.length(), ending);
        
        return false;
    };

    if (ends_with(path, ".longname"))
        return true; // 长文件名记录文件，直接忽略

    //没有标记忽略前一条，则检查是否长文件目录
    if (!ignored) {
        // 向上找到一个当前文件的挂载点且匹配文件系统类型
        if (partitions_.path_match_type(path, "fuse.dlnfs")) {
            // 长文件目录，标记此条事件被忽略
            return true;
        }
    }

    return false;
}

} // namespace anything