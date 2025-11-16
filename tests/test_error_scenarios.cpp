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

TEST(RiskManagerErrorTest, HandlesNullPositionManager) {
  RiskManager risk_manager(nullptr);

  Order test_order{};
  test_order.quantity = 100;
  test_order.price = 1000;

  EXPECT_FALSE(risk_manager.onNewOrder(test_order));
}

TEST(PositionManagerConcurrencyTest, HandlesMultiThreadedUpdates) {
  PositionManager pm;
  constexpr int num_threads = 4;

  std::vector<std::thread> threads;

  for (int i = 0; i < num_threads; ++i) {
    constexpr int fills_per_thread = 1000;
    threads.emplace_back([&pm, i]() {
      Fill fill{};
      std::memset(fill.symbol, 0, sizeof(fill.symbol));
      std::memcpy(fill.symbol, "AAPL", 4);
      fill.side = (i % 2 == 0) ? OrderSide::Buy : OrderSide::Sell;
      fill.quantity = 10;

      for (int j = 0; j < fills_per_thread; ++j) {
        pm.onFill(fill);
      }
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  const int64_t final_position = pm.getFilledPosition("AAPL");
  EXPECT_EQ(final_position, 0);
}
