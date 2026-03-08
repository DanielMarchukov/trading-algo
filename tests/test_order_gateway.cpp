#include "IRestClient.hpp"
#include "LockFreeMPSCQueue.hpp"
#include "Order.hpp"
#include "OrderGateway.hpp"
#include "PositionManager.hpp"
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

  std::promise<void> *promise_to_fulfill = nullptr;
};

class OrderGatewayTest : public ::testing::Test {
protected:
  std::shared_ptr<PositionManager> position_manager_ =
      std::make_shared<PositionManager>();
};

TEST_F(OrderGatewayTest, ProcessesOrderAndCallsRestClient) {
  std::atomic is_running(true);
  const auto order_queue = std::make_shared<LockFreeMPSCQueue<Order>>();
  std::promise<void> promise;
  const auto future = promise.get_future();

  auto mock_client = std::make_unique<MockRestClient>(&promise);
  OrderGateway gateway(is_running, order_queue, std::move(mock_client),
                       position_manager_);

  ThreadGuard gateway_thread_guard{std::thread(&OrderGateway::run, &gateway)};

  Order test_order{};
  test_order.id = 999;
  order_queue->push(test_order);

  const auto status = future.wait_for(std::chrono::seconds(2));
  ASSERT_EQ(status, std::future_status::ready);

  is_running.store(false);
}

TEST_F(OrderGatewayTest, DrainsPendingOrdersWhenStopping) {
  std::atomic is_running(true);
  const auto order_queue = std::make_shared<LockFreeMPSCQueue<Order>>();
  std::promise<void> promise;
  auto future = promise.get_future();

  auto mock_client = std::make_unique<MockRestClient>(&promise);
  OrderGateway gateway(is_running, order_queue, std::move(mock_client),
                       position_manager_);

  ThreadGuard gateway_thread_guard{std::thread(&OrderGateway::run, &gateway)};

  Order test_order{};
  test_order.id = 1234;
  order_queue->push(test_order);

  is_running.store(false);

  const auto status = future.wait_for(std::chrono::seconds(2));
  ASSERT_EQ(status, std::future_status::ready);
}

TEST_F(OrderGatewayTest, AssignsSequentialOrderIds) {
  std::atomic is_running(false);
  const auto order_queue = std::make_shared<LockFreeMPSCQueue<Order>>();

  std::vector<uint64_t> received_ids;
  class CapturingClient final : public IRestClient {
  public:
    explicit CapturingClient(std::vector<uint64_t> &ids) : ids_(ids) {}
    std::expected<OrderAck, OrderError>
    placeOrder(const Order &order) override {
      ids_.push_back(order.id);
      return OrderAck{"ord-" + std::to_string(order.id), "accepted"};
    }

  private:
    std::vector<uint64_t> &ids_;
  };

  Order order1{};
  Order order2{};
  order_queue->push(order1);
  order_queue->push(order2);

  OrderGateway gateway(is_running, order_queue,
                       std::make_unique<CapturingClient>(received_ids),
                       position_manager_);
  gateway.run();

  ASSERT_EQ(received_ids.size(), 2);
  EXPECT_EQ(received_ids[0], 1);
  EXPECT_EQ(received_ids[1], 2);
}

TEST_F(OrderGatewayTest, ReleasesPendingOnRejection) {
  std::atomic is_running(false);
  const auto order_queue = std::make_shared<LockFreeMPSCQueue<Order>>();

  position_manager_->registerSymbol("AAPL");

  Order order{};
  order.id = 1;
  std::memcpy(order.symbol, "AAPL", 4);
  order.side = OrderSide::Buy;
  order.quantity = 100;

  position_manager_->onOrderSent(order);
  EXPECT_EQ(position_manager_->getPendingPosition("AAPL"), 100);

  order_queue->push(order);

  class RejectingClient final : public IRestClient {
  public:
    std::expected<OrderAck, OrderError>
    placeOrder(const Order & /*unused*/) override {
      return std::unexpected(OrderError{422, "insufficient qty"});
    }
  };

  OrderGateway gateway(is_running, order_queue,
                       std::make_unique<RejectingClient>(), position_manager_);
  gateway.run();

  EXPECT_EQ(position_manager_->getPendingPosition("AAPL"), 0);
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
  std::shared_ptr<PositionManager> position_manager_ =
      std::make_shared<PositionManager>();
};

TEST_P(GatewayMainLoopExceptionTest, LogsCorrectError) {
  const auto &[factory, expected_substr] = GetParam();
  std::atomic is_running(true);
  const auto order_queue = std::make_shared<LockFreeMPSCQueue<Order>>();
  std::promise<void> promise;
  auto future = promise.get_future();

  auto client = factory(&promise);
  OrderGateway gateway(is_running, order_queue, std::move(client),
                       position_manager_);

  std::stringstream captured;
  auto *original = std::cerr.rdbuf(captured.rdbuf());

  ThreadGuard guard{std::thread(&OrderGateway::run, &gateway)};

  Order order{};
  order.id = 1;
  order_queue->push(order);

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
  std::shared_ptr<PositionManager> position_manager_ =
      std::make_shared<PositionManager>();
};

TEST_P(GatewayShutdownExceptionTest, LogsCorrectError) {
  const auto &[factory, expected_substr] = GetParam();
  std::atomic is_running(false);
  const auto order_queue = std::make_shared<LockFreeMPSCQueue<Order>>();

  Order order{};
  order.id = 1;
  order_queue->push(order);

  auto client = factory();
  OrderGateway gateway(is_running, order_queue, std::move(client),
                       position_manager_);

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
