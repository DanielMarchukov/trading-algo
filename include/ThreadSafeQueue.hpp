#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

template <typename T> class ThreadSafeQueue {
  public:
    void push(const T &item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(item);
        cond_var_.notify_one();
    }

    bool try_pop(T &item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        item = queue_.front();
        queue_.pop();
        return true;
    }

    void wait_and_pop(T &item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_var_.wait(lock, [this] { return !queue_.empty(); });
        item = queue_.front();
        queue_.pop();
    }

  private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cond_var_;
};
