#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

template <typename T> class LockFreeMPSCQueue {
private:
  struct alignas(64) Node {
    std::atomic<Node *> next;
    T data;

    explicit Node(const T &value) : next(nullptr), data(value) {}
    Node() : next(nullptr), data() {}

    static_assert(sizeof(T) <= 64,
                  "Queue element must fit in a single cache line");
  };

  alignas(64) std::atomic<Node *> head_;
  alignas(64) Node *tail_;
  alignas(64) Node stub_;

public:
  LockFreeMPSCQueue() : head_(&stub_), tail_(&stub_) {
    stub_.next.store(nullptr, std::memory_order_relaxed);
  }

  ~LockFreeMPSCQueue() {
    T dummy;
    while (try_pop(dummy)) {
    }
    if (tail_ != &stub_) {
      delete tail_;
    }
  }

  LockFreeMPSCQueue(const LockFreeMPSCQueue &) = delete;
  LockFreeMPSCQueue &operator=(const LockFreeMPSCQueue &) = delete;
  LockFreeMPSCQueue(LockFreeMPSCQueue &&) = delete;
  LockFreeMPSCQueue &operator=(LockFreeMPSCQueue &&) = delete;

  void push(const T &value) {
    Node *node = new Node(value);
    node->next.store(nullptr, std::memory_order_relaxed);

    Node *prev_head = head_.exchange(node, std::memory_order_acq_rel);
    prev_head->next.store(node, std::memory_order_release);
  }

  [[nodiscard]] bool try_pop(T &result) {
    Node *tail = tail_;
    Node *next = tail->next.load(std::memory_order_acquire);

    if (next == nullptr) {
      return false;
    }

    result = next->data;
    tail_ = next;

    if (tail != &stub_) {
      delete tail;
    }

    return true;
  }

  void wait_and_pop(T &result) {
    Node *tail = tail_;
    Node *next = tail->next.load(std::memory_order_acquire);

    while (next == nullptr) {
      next = tail->next.load(std::memory_order_acquire);
    }

    result = next->data;
    tail_ = next;

    if (tail != &stub_) {
      delete tail;
    }
  }

  [[nodiscard]] bool wait_and_pop(T &result, const std::atomic<bool> &running) {
    Node *tail = tail_;
    Node *next = tail->next.load(std::memory_order_acquire);

    while (next == nullptr) {
      if (!running.load(std::memory_order_relaxed) &&
          tail == head_.load(std::memory_order_acquire)) {
        return false;
      }
      next = tail->next.load(std::memory_order_acquire);
    }

    result = next->data;
    tail_ = next;

    if (tail != &stub_) {
      delete tail;
    }
    return true;
  }
};
