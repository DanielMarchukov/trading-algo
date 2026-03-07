#include "TradingEngine.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

class ThreadPinningTest : public ::testing::Test {};

TEST_F(ThreadPinningTest, PinThreadToCore0) {
  std::atomic<bool> thread_started{false};
  std::thread test_thread([&]() {
    thread_started.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  });

  // Wait for thread to start
  while (!thread_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  // Pin to core 0 - should not throw or crash
  EXPECT_NO_THROW(pin_thread_to_core(test_thread, 0));

  test_thread.join();
}

TEST_F(ThreadPinningTest, PinThreadToCore1) {
  std::atomic<bool> thread_started{false};
  std::thread test_thread([&]() {
    thread_started.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  });

  while (!thread_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  EXPECT_NO_THROW(pin_thread_to_core(test_thread, 1));

  test_thread.join();
}

TEST_F(ThreadPinningTest, PinMultipleThreadsToDifferentCores) {
  constexpr int num_threads = 4;
  std::vector<std::thread> threads;
  std::atomic<int> started{0};

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&]() {
      started.fetch_add(1, std::memory_order_relaxed);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });
  }

  // Wait for all threads to start
  while (started.load(std::memory_order_relaxed) < num_threads) {
    std::this_thread::yield();
  }

  // Pin each thread to a different core
  for (size_t i = 0; i < threads.size(); ++i) {
    EXPECT_NO_THROW(pin_thread_to_core(threads[i], i));
  }

  for (auto &t : threads) {
    t.join();
  }
}

TEST_F(ThreadPinningTest, PinToHighCoreNumber) {
  std::atomic<bool> thread_started{false};
  std::thread test_thread([&]() {
    thread_started.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  });

  while (!thread_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  // Pin to a high core number - may not exist on all systems, but should handle
  // gracefully
  EXPECT_NO_THROW(pin_thread_to_core(test_thread, 15));

  test_thread.join();
}

TEST_F(ThreadPinningTest, PinBeforeThreadStarts) {
  std::thread test_thread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  });

  // Pin immediately after thread creation
  EXPECT_NO_THROW(pin_thread_to_core(test_thread, 0));

  test_thread.join();
}

TEST_F(ThreadPinningTest, PinSameThreadMultipleTimes) {
  std::atomic<bool> thread_started{false};
  std::thread test_thread([&]() {
    thread_started.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  });

  while (!thread_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  // Pin to core 0
  EXPECT_NO_THROW(pin_thread_to_core(test_thread, 0));

  // Re-pin to core 1
  EXPECT_NO_THROW(pin_thread_to_core(test_thread, 1));

  test_thread.join();
}

TEST_F(ThreadPinningTest, ThreadExecutesAfterPinning) {
  std::atomic<int> counter{0};
  std::thread test_thread([&]() {
    for (int i = 0; i < 100; ++i) {
      counter.fetch_add(1, std::memory_order_relaxed);
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  });

  // Pin thread
  pin_thread_to_core(test_thread, 0);

  test_thread.join();

  // Verify thread executed correctly after pinning
  EXPECT_EQ(counter.load(std::memory_order_relaxed), 100);
}

TEST_F(ThreadPinningTest, PinShortLivedThread) {
  std::thread test_thread([]() {
    // Very short-lived thread
    volatile int x = 42;
    (void)x;
  });

  // Attempt to pin - may or may not complete before thread exits
  EXPECT_NO_THROW(pin_thread_to_core(test_thread, 0));

  test_thread.join();
}