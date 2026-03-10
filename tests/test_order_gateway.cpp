#include "IRestClient.hpp"
#include "LockFreeMPSCQueue.hpp"
#include "Order.hpp"
#include "OrderGateway.hpp"
#include "PendingOrderTracker.hpp"
#include "PositionManager.hpp"
#include "TestHelpers.hpp"
#include "ThreadGuard.hpp"
#include <expected>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <thread>

class MockRestClient final : public IRestClient {
public:
  explicit MockRestClient(std::promise<void> *p = nullptr)
      : promise_to_fulfill(p) {}

  std::expected<OrderAck, OrderError>
  placeOrder(const Order & /*unused*/) override {
    if (promise_to_fulfill) {
      promise_to_fulfill->set_value();
    }
    return OrderAck{"ord-123", "accepted"};
  }

  std::expected<void, OrderError>
  cancelOrder(std::string_view /*alpaca_order_id*/) override {
    return {};
  }

  std::promise<void> *promise_to_fulfill = nullptr;
};

class OrderGatewayTest : public ::testing::Test {
protected:
  PositionManager position_manager_;
};

TEST_F(OrderGatewayTest, ThrowsOnNullOrderQueue) {
  std::atomic is_running(true);
  EXPECT_THROW(OrderGateway(is_running, nullptr,
                            std::make_unique<MockRestClient>(),
                            &position_manager_),
               std::invalid_argument);
}

TEST_F(OrderGatewayTest, ThrowsOnNullRestClient) {
  std::atomic is_running(true);
  LockFreeMPSCQueue<Order> order_queue;
  EXPECT_THROW(
      OrderGateway(is_running, &order_queue, nullptr, &position_manager_),
      std::invalid_argument);
}

TEST_F(OrderGatewayTest, ThrowsOnNullPositionManager) {
  std::atomic is_running(true);
  LockFreeMPSCQueue<Order> order_queue;
  EXPECT_THROW(OrderGateway(is_running, &order_queue,
                            std::make_unique<MockRestClient>(), nullptr),
               std::invalid_argument);
}

TEST_F(OrderGatewayTest, ProcessesOrderAndCallsRestClient) {
  std::atomic is_running(true);
  LockFreeMPSCQueue<Order> order_queue;
  std::promise<void> promise;
  const auto future = promise.get_future();

  auto mock_client = std::make_unique<MockRestClient>(&promise);
  OrderGateway gateway(is_running, &order_queue, std::move(mock_client),
                       &position_manager_);

  ThreadGuard gateway_thread_guard{std::thread(&OrderGateway::run, &gateway)};

  Order test_order{};
  test_order.id = 999;
  order_queue.push(test_order);

  const auto status = future.wait_for(std::chrono::seconds(2));
  ASSERT_EQ(status, std::future_status::ready);

  is_running.store(false);
}

TEST_F(OrderGatewayTest, DrainsPendingOrdersWhenStopping) {
  std::atomic is_running(true);
  LockFreeMPSCQueue<Order> order_queue;
  std::promise<void> promise;
  auto future = promise.get_future();

  auto mock_client = std::make_unique<MockRestClient>(&promise);
  OrderGateway gateway(is_running, &order_queue, std::move(mock_client),
                       &position_manager_);

  ThreadGuard gateway_thread_guard{std::thread(&OrderGateway::run, &gateway)};

  Order test_order{};
  test_order.id = 1234;
  order_queue.push(test_order);

  is_running.store(false);

  const auto status = future.wait_for(std::chrono::seconds(2));
  ASSERT_EQ(status, std::future_status::ready);
}

TEST_F(OrderGatewayTest, AssignsSequentialOrderIdsWhileRunning) {
  std::atomic is_running(true);
  LockFreeMPSCQueue<Order> order_queue;

  std::vector<uint64_t> received_ids;
  std::atomic<int> call_count{0};

  class CapturingClient final : public IRestClient {
  public:
    CapturingClient(std::vector<uint64_t> &ids, std::atomic<int> &count)
        : ids_(ids), count_(count) {}
    std::expected<OrderAck, OrderError>
    placeOrder(const Order &order) override {
      ids_.push_back(order.id);
      count_.fetch_add(1, std::memory_order_release);
      return OrderAck{"ord-" + std::to_string(order.id), "accepted"};
    }
    std::expected<void, OrderError>
    cancelOrder(std::string_view /*alpaca_order_id*/) override {
      return {};
    }

  private:
    std::vector<uint64_t> &ids_;
    std::atomic<int> &count_;
  };

  OrderGateway gateway(
      is_running, &order_queue,
      std::make_unique<CapturingClient>(received_ids, call_count),
      &position_manager_);
  ThreadGuard guard{std::thread(&OrderGateway::run, &gateway)};

  Order order1{};
  Order order2{};
  order_queue.push(order1);
  order_queue.push(order2);

  while (call_count.load(std::memory_order_acquire) < 2) {
  }
  is_running.store(false);
  guard.t.join();

  ASSERT_EQ(received_ids.size(), 2);
  EXPECT_EQ(received_ids[0], 1);
  EXPECT_EQ(received_ids[1], 2);
}

TEST_F(OrderGatewayTest, ReleasesPendingOnRejection) {
  std::atomic is_running(false);
  LockFreeMPSCQueue<Order> order_queue;

  position_manager_.registerSymbol("AAPL");

  Order order{};
  order.id = 1;
  std::memcpy(order.symbol, "AAPL", 4);
  order.side = OrderSide::Buy;
  order.quantity = 100;

  position_manager_.onOrderSent(order);
  EXPECT_EQ(position_manager_.getPendingPosition("AAPL"), 100);

  order_queue.push(order);

  class RejectingClient final : public IRestClient {
  public:
    std::expected<OrderAck, OrderError>
    placeOrder(const Order & /*unused*/) override {
      return std::unexpected(OrderError{422, "insufficient qty"});
    }
    std::expected<void, OrderError>
    cancelOrder(std::string_view /*alpaca_order_id*/) override {
      return {};
    }
  };

  OrderGateway gateway(is_running, &order_queue,
                       std::make_unique<RejectingClient>(), &position_manager_);
  gateway.run();

  EXPECT_EQ(position_manager_.getPendingPosition("AAPL"), 0);
}

namespace {

class ThrowingRestClient final : public IRestClient {
public:
  explicit ThrowingRestClient(std::promise<void> *p = nullptr) : promise_(p) {}

  std::expected<OrderAck, OrderError>
  placeOrder(const Order & /*unused*/) override {
    if (promise_)
      promise_->set_value();
    throw std::runtime_error("simulated rest error");
  }

  std::expected<void, OrderError>
  cancelOrder(std::string_view /*alpaca_order_id*/) override {
    return {};
  }

private:
  std::promise<void> *promise_;
};

class WildThrowingRestClient final : public IRestClient {
public:
  explicit WildThrowingRestClient(std::promise<void> *p = nullptr)
      : promise_(p) {}

  std::expected<OrderAck, OrderError>
  placeOrder(const Order & /*unused*/) override {
    if (promise_)
      promise_->set_value();
    throw 42;
  }

  std::expected<void, OrderError>
  cancelOrder(std::string_view /*alpaca_order_id*/) override {
    return {};
  }

private:
  std::promise<void> *promise_;
};

} // namespace

struct MainLoopExceptionParam {
  std::function<std::unique_ptr<IRestClient>(std::promise<void> *)> factory;
  const char *expected_substr;
};

class GatewayMainLoopExceptionTest
    : public ::testing::TestWithParam<MainLoopExceptionParam> {
protected:
  PositionManager position_manager_;
};

TEST_P(GatewayMainLoopExceptionTest, LogsCorrectError) {
  const auto &[factory, expected_substr] = GetParam();
  std::atomic is_running(true);
  LockFreeMPSCQueue<Order> order_queue;
  std::promise<void> promise;
  auto future = promise.get_future();

  auto client = factory(&promise);
  OrderGateway gateway(is_running, &order_queue, std::move(client),
                       &position_manager_);

  std::stringstream captured;
  auto *original = std::cerr.rdbuf(captured.rdbuf());

  ThreadGuard guard{std::thread(&OrderGateway::run, &gateway)};

  Order order{};
  order.id = 1;
  order_queue.push(order);

  const auto status = future.wait_for(std::chrono::seconds(2));
  ASSERT_EQ(status, std::future_status::ready);
  is_running.store(false);

  guard.t.join();
  std::cerr.rdbuf(original);

  EXPECT_TRUE(captured.str().find(expected_substr) != std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(
    ExceptionTypes, GatewayMainLoopExceptionTest,
    ::testing::Values(
        MainLoopExceptionParam{[](std::promise<void> *p) {
                                 return std::make_unique<ThrowingRestClient>(p);
                               },
                               "OrderGateway: Exception placing order:"},
        MainLoopExceptionParam{
            [](std::promise<void> *p) {
              return std::make_unique<WildThrowingRestClient>(p);
            },
            "OrderGateway: Unknown error placing order"}));

struct ShutdownExceptionParam {
  std::function<std::unique_ptr<IRestClient>()> factory;
  const char *expected_substr;
};

class GatewayShutdownExceptionTest
    : public ::testing::TestWithParam<ShutdownExceptionParam> {
protected:
  PositionManager position_manager_;
};

TEST_P(GatewayShutdownExceptionTest, LogsCorrectError) {
  const auto &[factory, expected_substr] = GetParam();
  std::atomic is_running(false);
  LockFreeMPSCQueue<Order> order_queue;

  Order order{};
  order.id = 1;
  order_queue.push(order);

  auto client = factory();
  OrderGateway gateway(is_running, &order_queue, std::move(client),
                       &position_manager_);

  std::stringstream captured;
  auto *original = std::cerr.rdbuf(captured.rdbuf());

  gateway.run();

  std::cerr.rdbuf(original);

  EXPECT_TRUE(captured.str().find(expected_substr) != std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(
    ExceptionTypes, GatewayShutdownExceptionTest,
    ::testing::Values(
        ShutdownExceptionParam{
            []() { return std::make_unique<ThrowingRestClient>(); },
            "OrderGateway: Exception placing order:"},
        ShutdownExceptionParam{
            []() { return std::make_unique<WildThrowingRestClient>(); },
            "OrderGateway: Unknown error placing order"}));

namespace {

class TrackingRestClient final : public IRestClient {
public:
  struct Call {
    enum Type { Place, Cancel } type;
    std::string id;
  };

  explicit TrackingRestClient(
      std::expected<void, OrderError> cancel_result = {})
      : cancel_result_(std::move(cancel_result)) {}

  std::expected<OrderAck, OrderError>
  placeOrder(const Order & /*order*/) override {
    std::string id = "alpaca-" + std::to_string(++place_counter_);
    calls.push_back({Call::Place, id});
    return OrderAck{id, "accepted"};
  }

  std::expected<void, OrderError>
  cancelOrder(std::string_view alpaca_order_id) override {
    calls.push_back({Call::Cancel, std::string(alpaca_order_id)});
    return cancel_result_;
  }

  std::vector<Call> calls;

private:
  int place_counter_ = 0;
  std::expected<void, OrderError> cancel_result_;
};

class ThrowingCancelClient final : public IRestClient {
public:
  std::expected<OrderAck, OrderError>
  placeOrder(const Order & /*order*/) override {
    std::string id = "alpaca-" + std::to_string(++place_counter_);
    place_ids.push_back(id);
    return OrderAck{id, "accepted"};
  }

  std::expected<void, OrderError>
  cancelOrder(std::string_view /*alpaca_order_id*/) override {
    throw std::runtime_error("cancel network error");
  }

  std::vector<std::string> place_ids;

private:
  int place_counter_ = 0;
};

} // namespace

class CancelBeforeReplaceTest : public ::testing::Test {
protected:
  PositionManager position_manager_;
  PendingOrderTracker tracker_;
};

TEST_F(CancelBeforeReplaceTest, CancelsPreviousBeforePlacingNew) {
  std::atomic is_running(false);
  LockFreeMPSCQueue<Order> order_queue;

  Order order1 = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  Order order2 = test_helpers::createOrder("AAPL", OrderSide::Buy, 200, 0);
  order_queue.push(order1);
  order_queue.push(order2);

  auto *raw_client = new TrackingRestClient();
  std::unique_ptr<IRestClient> client(raw_client);
  OrderGateway gateway(is_running, &order_queue, std::move(client),
                       &position_manager_, &tracker_);
  gateway.run();

  ASSERT_GE(raw_client->calls.size(), 3u);
  EXPECT_EQ(raw_client->calls[0].type, TrackingRestClient::Call::Place);
  EXPECT_EQ(raw_client->calls[1].type, TrackingRestClient::Call::Cancel);
  EXPECT_EQ(raw_client->calls[1].id, "alpaca-1");
  EXPECT_EQ(raw_client->calls[2].type, TrackingRestClient::Call::Place);
}

TEST_F(CancelBeforeReplaceTest, NoCancelWhenNoPreviousOrder) {
  std::atomic is_running(false);
  LockFreeMPSCQueue<Order> order_queue;

  Order order = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  order_queue.push(order);

  auto *raw_client = new TrackingRestClient();
  std::unique_ptr<IRestClient> client(raw_client);
  OrderGateway gateway(is_running, &order_queue, std::move(client),
                       &position_manager_, &tracker_);
  gateway.run();

  ASSERT_EQ(raw_client->calls.size(), 1u);
  EXPECT_EQ(raw_client->calls[0].type, TrackingRestClient::Call::Place);
}

TEST_F(CancelBeforeReplaceTest, PlacesOrderEvenWhenCancelFails422) {
  std::atomic is_running(false);
  LockFreeMPSCQueue<Order> order_queue;

  Order order1 = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  Order order2 = test_helpers::createOrder("AAPL", OrderSide::Buy, 200, 0);
  order_queue.push(order1);
  order_queue.push(order2);

  auto cancel_error = std::unexpected(OrderError{422, "order already filled"});
  auto *raw_client = new TrackingRestClient(cancel_error);
  std::unique_ptr<IRestClient> client(raw_client);
  OrderGateway gateway(is_running, &order_queue, std::move(client),
                       &position_manager_, &tracker_);
  gateway.run();

  int place_count = 0;
  for (const auto &call : raw_client->calls) {
    if (call.type == TrackingRestClient::Call::Place)
      ++place_count;
  }
  EXPECT_EQ(place_count, 2);
}

TEST_F(CancelBeforeReplaceTest, SkipsNewOrderWhenCancelThrows) {
  std::atomic is_running(false);
  LockFreeMPSCQueue<Order> order_queue;

  position_manager_.registerSymbol("AAPL");

  Order order1 = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  Order order2 = test_helpers::createOrder("AAPL", OrderSide::Buy, 200, 0);
  order_queue.push(order1);
  order_queue.push(order2);

  auto *raw_client = new ThrowingCancelClient();
  std::unique_ptr<IRestClient> client(raw_client);
  OrderGateway gateway(is_running, &order_queue, std::move(client),
                       &position_manager_, &tracker_);
  gateway.run();

  // First order placed, second skipped because cancel threw
  EXPECT_EQ(raw_client->place_ids.size(), 1u);
}

TEST_F(CancelBeforeReplaceTest, TracksDifferentSidesIndependently) {
  std::atomic is_running(false);
  LockFreeMPSCQueue<Order> order_queue;

  Order buy1 = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  Order sell1 = test_helpers::createOrder("AAPL", OrderSide::Sell, 50, 0);
  order_queue.push(buy1);
  order_queue.push(sell1);

  auto *raw_client = new TrackingRestClient();
  std::unique_ptr<IRestClient> client(raw_client);
  OrderGateway gateway(is_running, &order_queue, std::move(client),
                       &position_manager_, &tracker_);
  gateway.run();

  ASSERT_EQ(raw_client->calls.size(), 2u);
  EXPECT_EQ(raw_client->calls[0].type, TrackingRestClient::Call::Place);
  EXPECT_EQ(raw_client->calls[1].type, TrackingRestClient::Call::Place);
}

TEST_F(CancelBeforeReplaceTest, DoesNotRecordFailedPlacement) {
  std::atomic is_running(false);
  LockFreeMPSCQueue<Order> order_queue;

  position_manager_.registerSymbol("AAPL");

  Order order = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  order_queue.push(order);

  class FailingPlaceClient final : public IRestClient {
  public:
    std::expected<OrderAck, OrderError>
    placeOrder(const Order & /*order*/) override {
      return std::unexpected(OrderError{500, "internal error"});
    }
    std::expected<void, OrderError>
    cancelOrder(std::string_view /*alpaca_order_id*/) override {
      return {};
    }
  };

  OrderGateway gateway(is_running, &order_queue,
                       std::make_unique<FailingPlaceClient>(),
                       &position_manager_, &tracker_);
  gateway.run();

  SymbolKey key{};
  std::memcpy(key.value, "AAPL", 4);
  EXPECT_FALSE(tracker_.getExistingOrder(key, OrderSide::Buy).has_value());
}
