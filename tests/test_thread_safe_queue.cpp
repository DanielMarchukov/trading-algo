#include "Order.hpp"
#include "ThreadSafeQueue.hpp"
#include <atomic>
#include <gtest/gtest.h>
#include <set>
#include <thread>
#include <vector>

class ThreadSafeQueueTest : public ::testing::Test {
protected:
  ThreadSafeQueue<Order> queue;
};

TEST_F(ThreadSafeQueueTest, SingleThreadedPushAndPop) {
  Order order_to_push{};
  order_to_push.id = 123;
  queue.push(order_to_push);

  Order popped_order{};
  queue.wait_and_pop(popped_order);

  EXPECT_EQ(popped_order.id, 123);
}

TEST_F(ThreadSafeQueueTest, TryPopBehavesCorrectly) {
  Order temp_order{};
  EXPECT_FALSE(queue.try_pop(temp_order));

  Order order_to_push{};
  order_to_push.id = 456;
  queue.push(order_to_push);

  EXPECT_TRUE(queue.try_pop(temp_order));
  EXPECT_EQ(temp_order.id, 456);
}

TEST_F(ThreadSafeQueueTest, MultiThreadedProducerConsumer) {
  constexpr int num_items = 1000;
  std::vector<Order> produced_orders;

  std::thread producer_thread([&]() {
    for (int i = 0; i < num_items; ++i) {
      Order order{};
      order.id = i;
      queue.push(order);
    }
  });
  std::thread consumer_thread([&]() {
    for (int i = 0; i < num_items; ++i) {
      Order order{};
      queue.wait_and_pop(order);
      produced_orders.push_back(order);
    }
  });

  producer_thread.join();
  consumer_thread.join();

  EXPECT_EQ(produced_orders.size(), num_items);
}

TEST_F(ThreadSafeQueueTest, MultiProducerSingleConsumer) {
  constexpr int num_producers = 4;
  constexpr int items_per_producer = 1000;
  constexpr int total_items = num_producers * items_per_producer;

  std::vector<std::thread> producers;
  std::atomic<bool> start{false};

  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([&, p]() {
      while (!start.load(std::memory_order_acquire)) {
      }
      for (int i = 0; i < items_per_producer; ++i) {
        Order order{};
        order.id = p * items_per_producer + i;
        queue.push(order);
      }
    });
  }

  start.store(true, std::memory_order_release);

  std::set<uint64_t> received;
  int consumed = 0;

  while (consumed < total_items) {
    Order order{};
    if (queue.try_pop(order)) {
      received.insert(order.id);
      ++consumed;
    }
  }

  for (auto &t : producers) {
    t.join();
  }

  EXPECT_EQ(received.size(), total_items);
  for (int i = 0; i < total_items; ++i) {
    EXPECT_TRUE(received.count(static_cast<uint64_t>(i)) > 0)
        << "Missing order id " << i;
  }

  Order leftover{};
  EXPECT_FALSE(queue.try_pop(leftover));
}

TEST_F(ThreadSafeQueueTest, WaitAndPopWithTimeout) {
  std::thread pusher([this]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    Order order{};
    order.id = 999;
    queue.push(order);
  });

  Order result{};
  queue.wait_and_pop(result);
  EXPECT_EQ(result.id, 999);

  pusher.join();
}
