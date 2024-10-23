#ifndef ANYTHING_EVENT_LISTENSER_H
#define ANYTHING_EVENT_LISTENSER_H

#include <functional>

#include <netlink/attr.h>
#include <netlink/handlers.h>

#include "vfs_genl.h"
#include "fs_event.h"


namespace anything {

using nl_sock_ptr = nl_sock*;
using nl_msg_ptr  = nl_msg*;

class event_listenser {
public:
    event_listenser();
    ~event_listenser();

    void start_listening();

    void stop_listening();

    void set_handler(std::function<void(fs_event)> handler);

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
    bool should_stop_;
    int fam_;
    std::function<void(fs_event)> handler_;
};

} // namespace anything

#endif // ANYTHING_EVENT_LISTENSER_H