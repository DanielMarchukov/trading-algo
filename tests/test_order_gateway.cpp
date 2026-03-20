#include "IRestClient.hpp"
#include "LatencyTracker.hpp"
#include "LockFreeMPSCQueue.hpp"
#include "Order.hpp"
#include "OrderGateway.hpp"
#include "PendingOrderTracker.hpp"
#include "PositionManager.hpp"
#include "TestHelpers.hpp"
#include "ThreadGuard.hpp"
#include "Utils.hpp"
#include <cstdint>
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

  std::expected<std::vector<AlpacaOrderStatus>, OrderError>
  queryOrders(std::string_view /*status_filter*/,
              std::string_view /*after*/) override {
    return std::vector<AlpacaOrderStatus>{};
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
    std::expected<std::vector<AlpacaOrderStatus>, OrderError>
    queryOrders(std::string_view /*status_filter*/,
                std::string_view /*after*/) override {
      return std::vector<AlpacaOrderStatus>{};
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

  ASSERT_EQ(received_ids.size(), 2);
  EXPECT_EQ(received_ids[0], 1);
  EXPECT_EQ(received_ids[1], 2);
}

struct PlaceRejectionParam {
  int64_t status_code;
  const char *message;
};

class PlaceRejectionTest
    : public OrderGatewayTest,
      public ::testing::WithParamInterface<PlaceRejectionParam> {};

TEST_P(PlaceRejectionTest, ReleasesPendingPosition) {
  const auto &[code, msg] = GetParam();
  std::atomic is_running(false);
  LockFreeMPSCQueue<Order> order_queue;

  position_manager_.registerSymbol("AAPL");

  Order order = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  order.id = 1;
  position_manager_.onOrderSent(order);
  EXPECT_EQ(position_manager_.getPendingPosition("AAPL"), 100);

  order_queue.push(order);

  class RejectingClient final : public IRestClient {
  public:
    RejectingClient(int64_t c, const char *m) : code_(c), msg_(m) {}
    std::expected<OrderAck, OrderError>
    placeOrder(const Order & /*unused*/) override {
      return std::unexpected(OrderError{code_, msg_});
    }
    std::expected<void, OrderError>
    cancelOrder(std::string_view /*alpaca_order_id*/) override {
      return {};
    }
    std::expected<std::vector<AlpacaOrderStatus>, OrderError>
    queryOrders(std::string_view /*status_filter*/,
                std::string_view /*after*/) override {
      return std::vector<AlpacaOrderStatus>{};
    }

  private:
    int64_t code_;
    const char *msg_;
  };

  OrderGateway gateway(is_running, &order_queue,
                       std::make_unique<RejectingClient>(code, msg),
                       &position_manager_);
  gateway.run();

  EXPECT_EQ(position_manager_.getPendingPosition("AAPL"), 0);
}

INSTANTIATE_TEST_SUITE_P(
    HttpErrors, PlaceRejectionTest,
    ::testing::Values(PlaceRejectionParam{422, "insufficient qty"},
                      PlaceRejectionParam{429, "rate limited"},
                      PlaceRejectionParam{500, "internal error"}));

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

  std::expected<std::vector<AlpacaOrderStatus>, OrderError>
  queryOrders(std::string_view /*status_filter*/,
              std::string_view /*after*/) override {
    return std::vector<AlpacaOrderStatus>{};
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

  std::expected<std::vector<AlpacaOrderStatus>, OrderError>
  queryOrders(std::string_view /*status_filter*/,
              std::string_view /*after*/) override {
    return std::vector<AlpacaOrderStatus>{};
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

  {
    ThreadGuard guard{std::thread(&OrderGateway::run, &gateway)};

    Order order{};
    order.id = 1;
    order_queue.push(order);

    const auto status = future.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);
    is_running.store(false);
  }

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
    enum Type : std::uint8_t { Place, Cancel } type;
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

  std::expected<std::vector<AlpacaOrderStatus>, OrderError>
  queryOrders(std::string_view /*status_filter*/,
              std::string_view /*after*/) override {
    return std::vector<AlpacaOrderStatus>{};
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

  std::expected<std::vector<AlpacaOrderStatus>, OrderError>
  queryOrders(std::string_view /*status_filter*/,
              std::string_view /*after*/) override {
    return std::vector<AlpacaOrderStatus>{};
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

struct CancelSkipParam {
  int64_t status_code;
  const char *message;
};

class CancelSkipTest : public CancelBeforeReplaceTest,
                       public ::testing::WithParamInterface<CancelSkipParam> {};

TEST_P(CancelSkipTest, SkipsNewOrderWhenCancelFails) {
  const auto &[code, msg] = GetParam();
  std::atomic is_running(false);
  LockFreeMPSCQueue<Order> order_queue;

  position_manager_.registerSymbol("AAPL");

  Order order1 = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  Order order2 = test_helpers::createOrder("AAPL", OrderSide::Buy, 200, 0);
  order_queue.push(order1);
  order_queue.push(order2);

  auto cancel_error = std::unexpected(OrderError{code, msg});
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
  EXPECT_EQ(place_count, 1);
}

INSTANTIATE_TEST_SUITE_P(
    CancelErrors, CancelSkipTest,
    ::testing::Values(CancelSkipParam{404, "order not found"},
                      CancelSkipParam{429, "rate limited"},
                      CancelSkipParam{500, "internal error"}));

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
    std::expected<std::vector<AlpacaOrderStatus>, OrderError>
    queryOrders(std::string_view /*status_filter*/,
                std::string_view /*after*/) override {
      return std::vector<AlpacaOrderStatus>{};
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

TEST_F(OrderGatewayTest, RecordsLatencyMetricsWhileRunning) {
  std::atomic is_running(true);
  LockFreeMPSCQueue<Order> order_queue;
  std::promise<void> promise;
  auto future = promise.get_future();
  LatencyTracker tracker;

  auto mock_client = std::make_unique<MockRestClient>(&promise);
  OrderGateway gateway(is_running, &order_queue, std::move(mock_client),
                       &position_manager_, nullptr, &tracker);

  ThreadGuard guard{std::thread(&OrderGateway::run, &gateway)};

  Order order{};
  order.arrivedAt = nowNanos();
  order.queuedAt = nowNanos();
  order_queue.push(order);

  auto status = future.wait_for(std::chrono::seconds(2));
  ASSERT_EQ(status, std::future_status::ready);
  is_running.store(false);

  EXPECT_EQ(tracker.count(LatencyMetric::MpscQueue), 1);
  EXPECT_EQ(tracker.count(LatencyMetric::EndToEnd), 1);
  EXPECT_GT(tracker.percentile(LatencyMetric::MpscQueue, 50.0), 0);
  EXPECT_GT(tracker.percentile(LatencyMetric::EndToEnd, 50.0), 0);
}

TEST_F(OrderGatewayTest, RecordsAllLatencyMetricsDuringDrain) {
  std::atomic is_running(false);
  LockFreeMPSCQueue<Order> order_queue;
  LatencyTracker tracker;

  Order order{};
  order.arrivedAt = nowNanos();
  order.queuedAt = nowNanos();
  order_queue.push(order);

  OrderGateway gateway(is_running, &order_queue,
                       std::make_unique<MockRestClient>(), &position_manager_,
                       nullptr, &tracker);
  gateway.run();

  EXPECT_EQ(tracker.count(LatencyMetric::MpscQueue), 1);
  EXPECT_EQ(tracker.count(LatencyMetric::EndToEnd), 1);

  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  tracker.recordFillReceived("ord-123");

  EXPECT_EQ(tracker.count(LatencyMetric::FillRoundTrip), 1);
}

namespace {

class SlowPlaceClient final : public IRestClient {
public:
  explicit SlowPlaceClient(std::chrono::milliseconds delay,
                           std::atomic<int> &count)
      : delay_(delay), count_(count) {}

  std::expected<OrderAck, OrderError>
  placeOrder(const Order & /*order*/) override {
    std::this_thread::sleep_for(delay_);
    count_.fetch_add(1, std::memory_order_release);
    return OrderAck{"ord-slow", "accepted"};
  }

  std::expected<void, OrderError>
  cancelOrder(std::string_view /*alpaca_order_id*/) override {
    return {};
  }

  std::expected<std::vector<AlpacaOrderStatus>, OrderError>
  queryOrders(std::string_view /*status_filter*/,
              std::string_view /*after*/) override {
    return std::vector<AlpacaOrderStatus>{};
  }

private:
  std::chrono::milliseconds delay_;
  std::atomic<int> &count_;
};

class SlowCancelClient final : public IRestClient {
public:
  explicit SlowCancelClient(std::chrono::milliseconds delay) : delay_(delay) {}

  std::expected<OrderAck, OrderError>
  placeOrder(const Order & /*order*/) override {
    return OrderAck{"ord-after-slow-cancel", "accepted"};
  }

  std::expected<void, OrderError>
  cancelOrder(std::string_view /*alpaca_order_id*/) override {
    std::this_thread::sleep_for(delay_);
    return {};
  }

  std::expected<std::vector<AlpacaOrderStatus>, OrderError>
  queryOrders(std::string_view /*status_filter*/,
              std::string_view /*after*/) override {
    return std::vector<AlpacaOrderStatus>{};
  }

private:
  std::chrono::milliseconds delay_;
};

} // namespace

TEST_F(OrderGatewayTest, SlowPlaceOrderCompletesWithoutHang) {
  std::atomic is_running(true);
  LockFreeMPSCQueue<Order> order_queue;
  std::atomic<int> placed{0};

  OrderGateway gateway(
      is_running, &order_queue,
      std::make_unique<SlowPlaceClient>(std::chrono::milliseconds(50), placed),
      &position_manager_);
  ThreadGuard guard{std::thread(&OrderGateway::run, &gateway)};

  order_queue.push(test_helpers::createOrder("AAPL", OrderSide::Buy, 10, 0));
  order_queue.push(test_helpers::createOrder("AAPL", OrderSide::Buy, 20, 0));

  while (placed.load(std::memory_order_acquire) < 2) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  is_running.store(false);

  EXPECT_EQ(placed.load(), 2);
}

TEST_F(CancelBeforeReplaceTest, SlowCancelCompletesWithoutHang) {
  std::atomic is_running(false);
  LockFreeMPSCQueue<Order> order_queue;

  Order order1 = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  Order order2 = test_helpers::createOrder("AAPL", OrderSide::Buy, 200, 0);
  order_queue.push(order1);
  order_queue.push(order2);

  OrderGateway gateway(
      is_running, &order_queue,
      std::make_unique<SlowCancelClient>(std::chrono::milliseconds(50)),
      &position_manager_, &tracker_);
  gateway.run();
}

TEST_F(CancelBeforeReplaceTest, CancelSucceedsButPlaceRateLimited) {
  std::atomic is_running(false);
  LockFreeMPSCQueue<Order> order_queue;

  position_manager_.registerSymbol("AAPL");

  Order order1 = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  Order order2 = test_helpers::createOrder("AAPL", OrderSide::Buy, 200, 0);
  position_manager_.onOrderSent(order1);
  position_manager_.onOrderSent(order2);

  order_queue.push(order1);
  order_queue.push(order2);

  class CancelOkPlaceRateLimited final : public IRestClient {
  public:
    std::expected<OrderAck, OrderError>
    placeOrder(const Order & /*order*/) override {
      if (++place_count_ == 2) {
        return std::unexpected(OrderError{429, "rate limited"});
      }
      return OrderAck{"alpaca-1", "accepted"};
    }
    std::expected<void, OrderError>
    cancelOrder(std::string_view /*alpaca_order_id*/) override {
      return {};
    }
    std::expected<std::vector<AlpacaOrderStatus>, OrderError>
    queryOrders(std::string_view /*status_filter*/,
                std::string_view /*after*/) override {
      return std::vector<AlpacaOrderStatus>{};
    }

  private:
    int place_count_ = 0;
  };

  OrderGateway gateway(is_running, &order_queue,
                       std::make_unique<CancelOkPlaceRateLimited>(),
                       &position_manager_, &tracker_);
  gateway.run();

  EXPECT_EQ(position_manager_.getPendingPosition("AAPL"), 100);
}

TEST_F(OrderGatewayTest, DrainReleasesAllPendingOnPersistentFailure) {
  std::atomic is_running(false);
  LockFreeMPSCQueue<Order> order_queue;

  position_manager_.registerSymbol("AAPL");

  constexpr int kOrderCount = 5;
  for (int i = 0; i < kOrderCount; ++i) {
    Order order = test_helpers::createOrder("AAPL", OrderSide::Buy, 10, 0);
    order.id = static_cast<uint64_t>(i) + 1;
    position_manager_.onOrderSent(order);
    order_queue.push(order);
  }

  EXPECT_EQ(position_manager_.getPendingPosition("AAPL"), 50);

  class AlwaysFailClient final : public IRestClient {
  public:
    std::expected<OrderAck, OrderError>
    placeOrder(const Order & /*order*/) override {
      return std::unexpected(OrderError{500, "server down"});
    }
    std::expected<void, OrderError>
    cancelOrder(std::string_view /*alpaca_order_id*/) override {
      return {};
    }
    std::expected<std::vector<AlpacaOrderStatus>, OrderError>
    queryOrders(std::string_view /*status_filter*/,
                std::string_view /*after*/) override {
      return std::vector<AlpacaOrderStatus>{};
    }
  };

  OrderGateway gateway(is_running, &order_queue,
                       std::make_unique<AlwaysFailClient>(),
                       &position_manager_);
  gateway.run();

  EXPECT_EQ(position_manager_.getPendingPosition("AAPL"), 0);
}
