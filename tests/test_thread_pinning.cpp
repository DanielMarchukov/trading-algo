#include "ThreadGuard.hpp"
#include "ThreadPinning.hpp"
#include <atomic>
#include <gtest/gtest.h>

TEST(ThreadPinningTest, PinsToValidCore) {
  std::atomic<bool> done{false};
  ThreadGuard guard{std::thread([&done] {
    while (!done.load(std::memory_order_acquire)) {
    }
  })};

  const bool result = pin_thread_to_core(guard.t, 0);
  done.store(true, std::memory_order_release);

#if defined(__linux__) || defined(__gnu_linux__) || defined(_WIN32)
  EXPECT_TRUE(result);
#elif defined(__APPLE__)
  (void)result;
#else
  EXPECT_FALSE(result);
#endif
}

TEST(ThreadPinningTest, RejectsExcessiveCoreId) {
#if !defined(__linux__) && !defined(__gnu_linux__) && !defined(_WIN32)
  GTEST_SKIP() << "No boundary validation on this platform";
#endif

  std::atomic<bool> done{false};
  ThreadGuard guard{std::thread([&done] {
    while (!done.load(std::memory_order_acquire)) {
    }
  })};

#if defined(__linux__) || defined(__gnu_linux__)
  const bool result = pin_thread_to_core(guard.t, CPU_SETSIZE);
  EXPECT_FALSE(result);
#elif defined(_WIN32)
  const bool result = pin_thread_to_core(guard.t, 64);
  EXPECT_FALSE(result);
#endif

  done.store(true, std::memory_order_release);
}
