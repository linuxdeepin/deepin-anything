// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_THREAD_POOL_HPP_
#define ANYTHING_THREAD_POOL_HPP_

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "common/anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

class thread_pool {
    using job_type = std::function<void()>;
public:
    thread_pool(unsigned int num = std::thread::hardware_concurrency());

    void enqueue_detach(job_type job);

    void wait_for_tasks();

    bool busy();

private:
    void thread_loop();

    bool stop_thread_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::vector<std::thread> threads_;
    std::queue<job_type> jobs_;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_THREAD_POOL_HPP_