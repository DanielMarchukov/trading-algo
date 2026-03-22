#include "ThreadPinning.hpp"
#include <gtest/gtest.h>
#include <thread>

TEST(ThreadPinningTest, PinsToValidCore) {
  std::thread t([] {});
  const bool result = pin_thread_to_core(t, 0);
#if defined(__linux__) || defined(__gnu_linux__) || defined(_WIN32)
  EXPECT_TRUE(result);
#elif defined(__APPLE__)
  // macOS: thread_policy_set is a hint, may succeed or fail
  (void)result;
#else
  EXPECT_FALSE(result);
#endif
  t.join();
}

TEST(ThreadPinningTest, RejectsExcessiveCoreId) {
  std::thread t([] {});
#if defined(__linux__) || defined(__gnu_linux__)
  const bool result = pin_thread_to_core(t, CPU_SETSIZE);
  EXPECT_FALSE(result);
#elif defined(_WIN32)
  const bool result = pin_thread_to_core(t, 64);
  EXPECT_FALSE(result);
#else
  GTEST_SKIP() << "No boundary validation on this platform";
#endif
  t.join();
}
