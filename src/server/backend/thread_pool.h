#ifndef ANYTHING_THREAD_POOL_HPP
#define ANYTHING_THREAD_POOL_HPP

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <vector>
#include <thread>

namespace anything {

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

} // namesapce anything

#endif // ANYTHING_THREAD_POOL_HPP