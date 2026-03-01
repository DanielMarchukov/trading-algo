#include "MarketEventConsumer.hpp"
#include "OrderGateway.hpp"
#include "RiskManager.hpp"
#include "Strategy.hpp"
#include <cstring>
#include <gtest/gtest.h>
#include <thread>

class ThrowingStrategy final : public Strategy {
public:
  static std::vector<Order> onMarketEvent(const MarketEvent &) {
    throw std::runtime_error("Strategy error");
  }
};

class ThrowingRestClient final : public IRestClient {
public:
  void placeOrder(const Order &) override {
    throw std::runtime_error("Network error");
  }
};

TEST(MarketEventConsumerErrorTest, HandlesStrategyException) {
  zmq::context_t context(1);
  zmq::socket_t publisher(context, zmq::socket_type::pub);
  publisher.bind("tcp://127.0.0.1:5556");

  std::atomic is_running(true);
  const auto risk_manager =
      std::make_shared<RiskManager>(std::make_shared<PositionManager>());

  bool callback_called = false;
  auto callback = [&](const Order &) { callback_called = true; };

  MarketEventConsumer<ThrowingStrategy> consumer(
      context, "tcp://127.0.0.1:5556", "TEST", is_running, callback,
      risk_manager);

  std::thread consumer_thread(&MarketEventConsumer<ThrowingStrategy>::run,
                              &consumer);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  MarketEvent event{};
  event.eventType = 2;
  std::memcpy(event.symbol, "TEST", 4);
  event.timestamp = 1;
  event.p1 = 100;
  event.s1 = 1;

  for (int i = 0; i < 5; ++i) {
    zmq::message_t topic("TEST", 4);
    zmq::message_t payload(&event, sizeof(MarketEvent));
    publisher.send(topic, zmq::send_flags::sndmore);
    publisher.send(payload, zmq::send_flags::none);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  is_running.store(false);
  consumer_thread.join();

  EXPECT_FALSE(callback_called);
}

TEST(OrderGatewayErrorTest, HandlesRestClientException) {
  std::atomic is_running(true);
  const auto order_queue = std::make_shared<ThreadSafeQueue<Order>>();
  auto throwing_client = std::make_unique<ThrowingRestClient>();

  OrderGateway gateway(is_running, order_queue, std::move(throwing_client));

  Order test_order{};
  test_order.id = 1;
  order_queue->push(test_order);

  std::thread gateway_thread(&OrderGateway::run, &gateway);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  is_running.store(false);
  gateway_thread.join();

  SUCCEED();
}

// HandlesNullPositionManager: covered by test_risk_manager.cpp
// HandlesMultiThreadedUpdates: covered by test_position_manager.cpp
