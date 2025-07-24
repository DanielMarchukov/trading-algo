#include "Order.hpp"
#include "ThreadSafeQueue.hpp"
#include <gtest/gtest.h>
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
