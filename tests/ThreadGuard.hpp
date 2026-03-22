#pragma once

#include <thread>

struct ThreadGuard {
  std::thread t;
  explicit ThreadGuard(std::thread &&thread) : t(std::move(thread)) {}
  ~ThreadGuard() {
    if (t.joinable()) {
      t.join();
    }
  }
  ThreadGuard(ThreadGuard &&other) noexcept : t(std::move(other.t)) {}
  ThreadGuard &operator=(ThreadGuard &&) = delete;
  ThreadGuard(const ThreadGuard &) = delete;
  ThreadGuard &operator=(const ThreadGuard &) = delete;
};
