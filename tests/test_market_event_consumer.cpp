#include "MarketEvent.hpp"
#include "MarketEventConsumer.hpp"
#include "Order.hpp"
#include "PositionManager.hpp"
#include "ThreadGuard.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <future>
#include <gtest/gtest.h>
#include <optional>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

class MockStrategy {
public:
  std::optional<Order> onMarketEvent(const MarketEvent &event) {
    if (event.eventType == 2) {
      Order order{};
      order.id = 1;
      order.side = OrderSide::Buy;
      order.type = OrderType::Market;
      order.quantity = 100;
      return order;
    }
    return std::nullopt;
  }
};

struct TestOrderCallback {
  int *count;
  void operator()(const Order & /*order*/) const { (*count)++; }
};

struct NoopOrderCallback {
  void operator()(const Order & /*order*/) const {}
};

struct PromiseOrderCallback {
  std::promise<Order> *promise;
  void operator()(const Order &order) const { promise->set_value(order); }
};

class MarketEventConsumerTest : public ::testing::Test {
protected:
  void SetUp() override {
#ifdef _WIN32
    ipc_address = "tcp://127.0.0.1:5555";
#else
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
    std::filesystem::path socket_path = temp_dir / ("test_market_data.sock");
    ipc_address = "ipc:///" + socket_path.string();
#endif
    position_manager_ = std::make_unique<PositionManager>();
    risk_manager_ = std::make_unique<RiskManager>(position_manager_.get());
  }

  std::string ipc_address;
  std::unique_ptr<PositionManager> position_manager_;
  std::unique_ptr<RiskManager> risk_manager_;
};

class EmptyStrategy {
public:
  std::optional<Order> onMarketEvent(const MarketEvent & /*event*/) {
    return std::nullopt;
  }
};

class RejectedOrderStrategy {
public:
  std::optional<Order> onMarketEvent(const MarketEvent &event) {
    if (event.eventType == 2) {
      Order order{};
      order.id = 1;
      order.side = OrderSide::Buy;
      order.quantity = 2000;
      order.price = 1000000;
      return order;
    }
    return std::nullopt;
  }
};

class CountingStrategy {
public:
  static std::atomic<int> invocation_count;

  std::optional<Order> onMarketEvent(const MarketEvent & /*event*/) {
    invocation_count.fetch_add(1, std::memory_order_relaxed);
    return std::nullopt;
  }
};

std::atomic<int> CountingStrategy::invocation_count{0};

TEST_F(MarketEventConsumerTest, HandlesStrategyReturningNoOrders) {
  zmq::context_t context(1);
  std::atomic is_running(true);
  int callback_count = 0;

  TestOrderCallback callback{&callback_count};

  try {
    MarketEventConsumer<EmptyStrategy, TestOrderCallback> consumer(
        context, ipc_address, "TEST", is_running, callback,
        risk_manager_.get());

    SUCCEED();
  } catch (const std::exception &e) {
    GTEST_SKIP() << "ZMQ connection failed: " << e.what();
  }
}

TEST_F(MarketEventConsumerTest, HandlesRiskManagerRejection) {
  zmq::context_t context(1);
  std::atomic is_running(true);
  int callback_count = 0;

  TestOrderCallback callback{&callback_count};

  try {
    MarketEventConsumer<RejectedOrderStrategy, TestOrderCallback> consumer(
        context, ipc_address, "TEST", is_running, callback,
        risk_manager_.get());

    SUCCEED();
  } catch (const std::exception &e) {
    GTEST_SKIP() << "ZMQ connection failed: " << e.what();
  }

  EXPECT_EQ(callback_count, 0);
}

TEST_F(MarketEventConsumerTest, ConstructorThrowsOnInvalidAddress) {
  zmq::context_t context(1);
  std::atomic is_running(true);
  NoopOrderCallback callback;

  EXPECT_THROW((MarketEventConsumer<EmptyStrategy, NoopOrderCallback>(
                   context, "invalid://address", "TEST", is_running, callback,
                   risk_manager_.get())),
               zmq::error_t);
}

TEST_F(MarketEventConsumerTest, ConstructorRejectsSymbolExceeding8Bytes) {
  zmq::context_t context(1);
  std::atomic is_running(true);
  NoopOrderCallback callback;

  EXPECT_THROW((MarketEventConsumer<EmptyStrategy, NoopOrderCallback>(
                   context, ipc_address, "TOOLONGSYM", is_running, callback,
                   risk_manager_.get())),
               std::invalid_argument);
}

TEST_F(MarketEventConsumerTest, CallsStrategyAndReceivesOrders) {
  zmq::context_t context(1);
  zmq::socket_t publisher(context, zmq::socket_type::pub);
  publisher.bind(ipc_address);

  std::atomic is_test_running(true);
  std::promise<Order> promise;
  auto future = promise.get_future();
  PromiseOrderCallback test_callback{&promise};

  MarketEventConsumer<MockStrategy, PromiseOrderCallback> consumer(
      context, ipc_address, "TEST", is_test_running, test_callback,
      risk_manager_.get());

  ThreadGuard consumer_thread_guard{
      std::thread(&MarketEventConsumer<MockStrategy, PromiseOrderCallback>::run,
                  &consumer)};

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  MarketEvent sent_event{};
  sent_event.eventType = 2;

  auto start_time = std::chrono::steady_clock::now();
  while (future.wait_for(std::chrono::milliseconds(10)) !=
         std::future_status::ready) {
    zmq::message_t topic("TEST", 4);
    zmq::message_t payload(&sent_event, sizeof(MarketEvent));
    publisher.send(topic, zmq::send_flags::sndmore);
    publisher.send(payload, zmq::send_flags::none);
    if (std::chrono::steady_clock::now() - start_time >
        std::chrono::seconds(2)) {
      break;
    }
  }

  is_test_running.store(false);

  ASSERT_TRUE(future.valid());
  auto status = future.wait_for(std::chrono::seconds(0));
  ASSERT_EQ(status, std::future_status::ready);
  Order received_order = future.get();
  EXPECT_EQ(received_order.id, 1);
  EXPECT_EQ(received_order.side, OrderSide::Buy);
  EXPECT_EQ(received_order.quantity, 100);
}

TEST_F(MarketEventConsumerTest, SkipsMismatchedPayloadSize) {
  zmq::context_t context(1);
  zmq::socket_t publisher(context, zmq::socket_type::pub);
  publisher.bind(ipc_address);

  CountingStrategy::invocation_count.store(0, std::memory_order_relaxed);

  std::atomic is_running(true);
  NoopOrderCallback callback;

  MarketEventConsumer<CountingStrategy, NoopOrderCallback> consumer(
      context, ipc_address, "TEST", is_running, callback, risk_manager_.get());

  ThreadGuard consumer_thread_guard{std::thread(
      &MarketEventConsumer<CountingStrategy, NoopOrderCallback>::run,
      &consumer)};

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  zmq::message_t topic("TEST", 4);
  zmq::message_t malformed_payload(sizeof(MarketEvent) + 4);
  std::memset(malformed_payload.data(), 0, malformed_payload.size());

  publisher.send(topic, zmq::send_flags::sndmore);
  publisher.send(malformed_payload, zmq::send_flags::none);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  is_running.store(false);

  EXPECT_EQ(CountingStrategy::invocation_count.load(std::memory_order_relaxed),
            0);
}
