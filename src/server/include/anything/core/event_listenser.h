#ifndef ANYTHING_EVENT_LISTENSER_H_
#define ANYTHING_EVENT_LISTENSER_H_

#include <atomic>
#include <functional>
#include <thread>

#include <netlink/attr.h>
#include <netlink/handlers.h>

#include "anything/common/anything_fwd.hpp"
#include "anything/common/fs_event.h"
#include "anything/common/vfs_genl.h"

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

    void set_handler(std::function<void(fs_event)> handler);

    void set_idle_task(std::function<void()> idle_task, int timeout = -1);

private:
    bool connect(nl_sock_ptr& sk);
    void disconnect(nl_sock_ptr& sk);
    bool set_callback(nl_sock_ptr& sk, nl_recvmsg_msg_cb_t func);
    int get_fd(nl_sock_ptr& sk) const;

    void forward_event_to_handler(fs_event event);

    static int event_handler(nl_msg_ptr msg, void* arg);

private:
    nl_sock_ptr mcsk_;
    bool connected_;
    std::atomic<bool> should_stop_;
    int fam_;
    int timeout_;
    std::function<void(fs_event)> handler_;
    std::function<void()> idle_task_;
    std::thread listening_thread_;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_EVENT_LISTENSER_H_