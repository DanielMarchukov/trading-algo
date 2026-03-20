#include "LockFreeMPSCQueue.hpp"
#include "ThreadGuard.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <future>
#include <gtest/gtest.h>
#include <set>
#include <thread>
#include <vector>

class LockFreeMPSCQueueTest : public ::testing::Test {
protected:
  LockFreeMPSCQueue<int> queue;
};

TEST_F(LockFreeMPSCQueueTest, EmptyQueueReturnsFalse) {
  int value;
  EXPECT_FALSE(queue.try_pop(value));
}

TEST_F(LockFreeMPSCQueueTest, SinglePushPop) {
  queue.push(42);
  int value;
  EXPECT_TRUE(queue.try_pop(value));
  EXPECT_EQ(value, 42);
}

TEST_F(LockFreeMPSCQueueTest, PopAfterPushLeavesEmpty) {
  queue.push(100);
  int value;
  EXPECT_TRUE(queue.try_pop(value));
  EXPECT_FALSE(queue.try_pop(value));
}

TEST_F(LockFreeMPSCQueueTest, MultiplePushPopSequential) {
  for (int i = 0; i < 100; ++i) {
    queue.push(i);
  }

  for (int i = 0; i < 100; ++i) {
    int value;
    EXPECT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, i);
  }

  int value;
  EXPECT_FALSE(queue.try_pop(value));
}

TEST_F(LockFreeMPSCQueueTest, InterleavedPushPop) {
  queue.push(1);
  queue.push(2);

  int value;
  EXPECT_TRUE(queue.try_pop(value));
  EXPECT_EQ(value, 1);

  queue.push(3);

  EXPECT_TRUE(queue.try_pop(value));
  EXPECT_EQ(value, 2);

  EXPECT_TRUE(queue.try_pop(value));
  EXPECT_EQ(value, 3);

  EXPECT_FALSE(queue.try_pop(value));
}

TEST_F(LockFreeMPSCQueueTest, WaitAndPopBlocks) {
  std::atomic<bool> consumer_done{false};
  int consumed_value = 0;
  std::promise<void> consumer_finished;

  ThreadGuard consumer{std::thread([&]() {
    queue.wait_and_pop(consumed_value);
    consumer_done.store(true);
    consumer_finished.set_value();
  })};

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_FALSE(consumer_done.load());

  queue.push(999);

  auto status =
      consumer_finished.get_future().wait_for(std::chrono::seconds(2));
  ASSERT_EQ(status, std::future_status::ready) << "timeout — possible deadlock";
  EXPECT_TRUE(consumer_done.load());
  EXPECT_EQ(consumed_value, 999);
}

TEST_F(LockFreeMPSCQueueTest, StoppableWaitAndPopReturnsOnData) {
  std::atomic<bool> running{true};
  int value = 0;
  bool got = false;
  std::promise<void> consumer_finished;

  ThreadGuard producer{std::thread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    queue.push(42);
  })};

  ThreadGuard consumer{std::thread([&]() {
    got = queue.wait_and_pop(value, running);
    consumer_finished.set_value();
  })};

  auto status =
      consumer_finished.get_future().wait_for(std::chrono::seconds(2));
  ASSERT_EQ(status, std::future_status::ready) << "timeout — possible deadlock";
  EXPECT_TRUE(got);
  EXPECT_EQ(value, 42);
}

TEST_F(LockFreeMPSCQueueTest, StoppableWaitAndPopExitsOnStop) {
  std::atomic<bool> running{true};
  std::atomic<bool> consumer_exited{false};
  bool got = true;
  int value = 0;
  std::promise<void> consumer_finished;

  ThreadGuard consumer{std::thread([&]() {
    got = queue.wait_and_pop(value, running);
    consumer_exited.store(true);
    consumer_finished.set_value();
  })};

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_FALSE(consumer_exited.load());

  running.store(false);

  auto status =
      consumer_finished.get_future().wait_for(std::chrono::seconds(2));
  ASSERT_EQ(status, std::future_status::ready) << "timeout — possible deadlock";
  EXPECT_TRUE(consumer_exited.load());
  EXPECT_FALSE(got);
}

TEST_F(LockFreeMPSCQueueTest, MultiProducerSingleConsumer) {
  constexpr int num_producers = 8;
  constexpr int items_per_producer = 1000;
  constexpr int total_items = num_producers * items_per_producer;

  std::vector<std::thread> producers;
  producers.reserve(num_producers);
  std::atomic<bool> start{false};
  std::promise<void> consumer_finished;

  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([&, p]() {
      while (!start.load(std::memory_order_acquire)) {
      }

      for (int i = 0; i < items_per_producer; ++i) {
        const int value = p * items_per_producer + i;
        queue.push(value);
      }
    });
  }

  start.store(true, std::memory_order_release);

  std::set<int> received;
  int consumed_count = 0;

  ThreadGuard consumer{std::thread([&]() {
    while (consumed_count < total_items) {
      int value;
      if (queue.try_pop(value)) {
        received.insert(value);
        ++consumed_count;
      }
    }
    consumer_finished.set_value();
  })};

  auto status =
      consumer_finished.get_future().wait_for(std::chrono::seconds(5));
  ASSERT_EQ(status, std::future_status::ready) << "timeout — possible deadlock";

  for (auto &t : producers) {
    t.join();
  }

  EXPECT_EQ(received.size(), total_items);
  for (int i = 0; i < total_items; ++i) {
    EXPECT_TRUE(received.count(i) > 0);
  }
}

TEST_F(LockFreeMPSCQueueTest, StressTestHighContention) {
  constexpr int num_producers = 16;
  constexpr int items_per_producer = 10000;
  constexpr int total_items = num_producers * items_per_producer;

  std::vector<std::thread> producers;
  producers.reserve(num_producers);
  std::atomic<int> production_counter{0};
  std::promise<void> consumer_finished;

  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([&]() {
      for (int i = 0; i < items_per_producer; ++i) {
        const int value =
            production_counter.fetch_add(1, std::memory_order_relaxed);
        queue.push(value);
      }
    });
  }

  std::vector<int> received;
  received.reserve(total_items);

  ThreadGuard consumer{std::thread([&]() {
    while (received.size() < static_cast<size_t>(total_items)) {
      int value;
      if (queue.try_pop(value)) {
        received.push_back(value);
      }
    }
    consumer_finished.set_value();
  })};

  auto status =
      consumer_finished.get_future().wait_for(std::chrono::seconds(30));
  ASSERT_EQ(status, std::future_status::ready) << "timeout — possible deadlock";

  for (auto &t : producers) {
    t.join();
  }

  std::ranges::sort(received);
  EXPECT_EQ(received.size(), total_items);

  for (size_t i = 0; i < received.size(); ++i) {
    EXPECT_EQ(received[i], static_cast<int>(i));
  }
}

TEST_F(LockFreeMPSCQueueTest, MixedPushPopWithDelay) {
  std::atomic<bool> producer_done{false};
  std::promise<void> consumer_finished;

  ThreadGuard producer{std::thread([&]() {
    for (int i = 0; i < 1000; ++i) {
      queue.push(i);
      if (i % 100 == 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
    }
    producer_done.store(true);
  })};

  int consumed_count = 0;
  int last_value = -1;

  ThreadGuard consumer{std::thread([&]() {
    while (!producer_done.load() || consumed_count < 1000) {
      int value;
      if (queue.try_pop(value)) {
        EXPECT_GT(value, last_value);
        last_value = value;
        ++consumed_count;
      }
    }
    consumer_finished.set_value();
  })};

  auto status =
      consumer_finished.get_future().wait_for(std::chrono::seconds(5));
  ASSERT_EQ(status, std::future_status::ready) << "timeout — possible deadlock";
  EXPECT_EQ(consumed_count, 1000);
}

struct ComplexData {
  int id;
  double price;
  char symbol[8];

  bool operator==(const ComplexData &other) const {
    return id == other.id && price == other.price &&
           std::memcmp(symbol, other.symbol, 8) == 0;
  }
};

TEST(LockFreeMPSCQueueComplexTest, HandlesComplexTypes) {
  LockFreeMPSCQueue<ComplexData> complex_queue;

  ComplexData data1{1, 100.5, "AAPL"};
  ComplexData data2{2, 200.75, "GOOGL"};

  complex_queue.push(data1);
  complex_queue.push(data2);

  ComplexData result{};
  EXPECT_TRUE(complex_queue.try_pop(result));
  EXPECT_EQ(result, data1);

  EXPECT_TRUE(complex_queue.try_pop(result));
  EXPECT_EQ(result, data2);

  EXPECT_FALSE(complex_queue.try_pop(result));
}
