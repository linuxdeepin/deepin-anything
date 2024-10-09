#ifndef ANYTHING_EVENT_LISTENSER_H
#define ANYTHING_EVENT_LISTENSER_H

#include "partition.h"
#include "vfs_genl.h"
#include "thread_pool.h"
#include <string>
#include <netlink/attr.h>
#include <netlink/handlers.h>


namespace anything {

using nl_sock_ptr = nl_sock*;
using nl_msg_ptr  = nl_msg*;


struct fs_event {
    uint8_t     act;
    uint32_t    cookie;
    uint16_t    major;
    uint8_t     minor;
    std::string src;
    std::string dst;
};


class event_listenser {
public:
    event_listenser();
    ~event_listenser();

    void listen();
    void push_fs_event(fs_event event);

private:
    bool connect(nl_sock_ptr& sk);
    void disconnect(nl_sock_ptr& sk);
    bool set_callback(nl_sock_ptr& sk, nl_recvmsg_msg_cb_t func);
    int get_fd(nl_sock_ptr& sk) const;

    template<class T>
    bool partition_contains(const T& key) {
        std::lock_guard<std::mutex> lock(mtx_);
        return partitions_.contains(key);
    }

    template<class T>
    auto partition_get(const T& key) {
        std::lock_guard<std::mutex> lock(mtx_);
        return partitions_[key];
    }


    static int event_handler(nl_msg_ptr msg, void* arg);
    
    // Process the fs events
    void fs_event_handler(fs_event event);

    bool ignored_event(const std::string& path, bool ignored);

private:
    nl_sock_ptr mcsk_;
    bool connected_;
    int fam_;
    partition partitions_;
    thread_pool pool_;
    std::mutex mtx_;
};

} // namespace anything

#endif // ANYTHING_EVENT_LISTENSER_H