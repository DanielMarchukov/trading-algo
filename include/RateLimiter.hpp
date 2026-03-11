#pragma once

#include <atomic>
#include <cstdint>

enum class ThrottlePolicy : uint8_t {
  Wait,
  Drop,
};

class RateLimiter {
public:
  static constexpr int64_t kMaxRequestsPerWindow = 200;
  static constexpr int64_t kDefaultThreshold = 10;
  static constexpr int64_t kWindowDurationNs = 60'000'000'000LL;

  explicit RateLimiter(int64_t threshold = kDefaultThreshold,
                       ThrottlePolicy policy = ThrottlePolicy::Wait) noexcept;

  void update(int64_t remaining) noexcept;
  void onRateLimited() noexcept;

  [[nodiscard]] bool canSend() const noexcept;

  [[nodiscard]] bool waitOrDrop() const;

  [[nodiscard]] int64_t remaining() const noexcept;
  [[nodiscard]] int64_t threshold() const noexcept;
  [[nodiscard]] ThrottlePolicy policy() const noexcept;

private:
  std::atomic<int64_t> remaining_{kMaxRequestsPerWindow};
  mutable std::atomic<int64_t> throttle_start_ns_{0};
  int64_t threshold_;
  ThrottlePolicy policy_;
};
