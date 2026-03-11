#include "RateLimiter.hpp"
#include "Utils.hpp"
#include <thread>

RateLimiter::RateLimiter(int64_t threshold, ThrottlePolicy policy) noexcept
    : threshold_(threshold), policy_(policy) {}

void RateLimiter::update(int64_t remaining) noexcept {
  remaining_.store(remaining, std::memory_order_relaxed);
  if (remaining < threshold_) {
    int64_t expected = 0;
    throttle_start_ns_.compare_exchange_strong(expected, nowNanos(),
                                               std::memory_order_relaxed);
  } else {
    throttle_start_ns_.store(0, std::memory_order_relaxed);
  }
}

void RateLimiter::onRateLimited() noexcept {
  remaining_.store(0, std::memory_order_relaxed);
  int64_t expected = 0;
  throttle_start_ns_.compare_exchange_strong(expected, nowNanos(),
                                             std::memory_order_relaxed);
}

bool RateLimiter::canSend() const noexcept {
  if (remaining_.load(std::memory_order_relaxed) >= threshold_) {
    return true;
  }
  const int64_t start = throttle_start_ns_.load(std::memory_order_relaxed);
  if (start == 0) {
    return true;
  }
  if (static_cast<int64_t>(nowNanos()) - start >= kWindowDurationNs) {
    throttle_start_ns_.store(0, std::memory_order_relaxed);
    return true;
  }
  return false;
}

bool RateLimiter::waitOrDrop() const {
  if (canSend()) {
    return true;
  }
  if (policy_ == ThrottlePolicy::Drop) {
    return false;
  }
  while (!canSend()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return true;
}

int64_t RateLimiter::remaining() const noexcept {
  return remaining_.load(std::memory_order_relaxed);
}

int64_t RateLimiter::threshold() const noexcept { return threshold_; }

ThrottlePolicy RateLimiter::policy() const noexcept { return policy_; }
