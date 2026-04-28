#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// Simple fixed-size thread pool with a FIFO task queue.
// Tasks are dispatched in enqueue order; with n_threads == 1
// execution is strictly sequential.
class ThreadPool {
public:
    explicit ThreadPool(size_t n_threads) {
        workers_.reserve(n_threads);
        for (size_t i = 0; i < n_threads; ++i)
            workers_.emplace_back(&ThreadPool::workerLoop, this);
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        task_cv_.notify_all();
        for (auto& w : workers_)
            w.join();
    }

    // Enqueue a task. Tasks are executed in FIFO order.
    void enqueue(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push(std::move(task));
            ++pending_;
        }
        task_cv_.notify_one();
    }

    // Block until all enqueued tasks have completed.
    void waitAll() {
        std::unique_lock<std::mutex> lock(mutex_);
        done_cv_.wait(lock, [this] { return pending_ == 0; });
    }

    // Block until all tasks complete or the deadline is reached.
    // Returns true if all tasks finished, false on timeout.
    template<typename Clock, typename Duration>
    bool waitUntil(const std::chrono::time_point<Clock, Duration>& deadline) {
        std::unique_lock<std::mutex> lock(mutex_);
        return done_cv_.wait_until(lock, deadline, [this] { return pending_ == 0; });
    }

private:
    void workerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                task_cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty())
                    return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (--pending_ == 0)
                    done_cv_.notify_all();
            }
        }
    }

    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        mutex_;
    std::condition_variable           task_cv_;
    std::condition_variable           done_cv_;
    bool                              stop_    = false;
    int                               pending_ = 0;
};

#endif // THREAD_POOL_H