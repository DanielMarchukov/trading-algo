#include "IRestClient.hpp"
#include "Order.hpp"
#include "OrderGateway.hpp"
#include "ThreadGuard.hpp"
#include "ThreadSafeQueue.hpp"
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <thread>

class MockRestClient final : public IRestClient {
public:
  explicit MockRestClient(std::promise<void> *p = nullptr)
      : promise_to_fulfill(p) {}

  void placeOrder(const Order & /*unused*/) override {
    if (promise_to_fulfill) {
      promise_to_fulfill->set_value();
    }
  }

  std::promise<void> *promise_to_fulfill = nullptr;
};

TEST(OrderGatewayTest, ProcessesOrderAndCallsRestClient) {
  std::atomic is_running(true);
  const auto order_queue = std::make_shared<ThreadSafeQueue<Order>>();
  std::promise<void> promise;
  const auto future = promise.get_future();

  auto mock_client = std::make_unique<MockRestClient>(&promise);
  OrderGateway gateway(is_running, order_queue, std::move(mock_client));

  ThreadGuard gateway_thread_guard{std::thread(&OrderGateway::run, &gateway)};

  Order test_order{};
  test_order.id = 999;
  order_queue->push(test_order);

  const auto status = future.wait_for(std::chrono::seconds(2));
  ASSERT_EQ(status, std::future_status::ready);

  is_running.store(false);
}

TEST(OrderGatewayTest, DrainsPendingOrdersWhenStopping) {
  std::atomic is_running(true);
  const auto order_queue = std::make_shared<ThreadSafeQueue<Order>>();
  std::promise<void> promise;
  auto future = promise.get_future();

  auto mock_client = std::make_unique<MockRestClient>(&promise);
  OrderGateway gateway(is_running, order_queue, std::move(mock_client));

  ThreadGuard gateway_thread_guard{std::thread(&OrderGateway::run, &gateway)};

  Order test_order{};
  test_order.id = 1234;
  order_queue->push(test_order);

  is_running.store(false);

  const auto status = future.wait_for(std::chrono::seconds(2));
  ASSERT_EQ(status, std::future_status::ready);
}

namespace {

class ThrowingRestClient final : public IRestClient {
public:
  explicit ThrowingRestClient(std::promise<void> *p = nullptr) : promise_(p) {}

  void placeOrder(const Order & /*unused*/) override {
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

  void placeOrder(const Order & /*unused*/) override {
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
    : public ::testing::TestWithParam<MainLoopExceptionParam> {};

TEST_P(GatewayMainLoopExceptionTest, LogsCorrectError) {
  const auto &[factory, expected_substr] = GetParam();
  std::atomic is_running(true);
  const auto order_queue = std::make_shared<ThreadSafeQueue<Order>>();
  std::promise<void> promise;
  auto future = promise.get_future();

  auto client = factory(&promise);
  OrderGateway gateway(is_running, order_queue, std::move(client));

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
                               "OrderGateway: Error placing order:"},
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
    : public ::testing::TestWithParam<ShutdownExceptionParam> {};

TEST_P(GatewayShutdownExceptionTest, LogsCorrectError) {
  const auto &[factory, expected_substr] = GetParam();
  std::atomic is_running(false);
  const auto order_queue = std::make_shared<ThreadSafeQueue<Order>>();

  Order order{};
  order.id = 1;
  order_queue->push(order);

  auto client = factory();
  OrderGateway gateway(is_running, order_queue, std::move(client));

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
            "OrderGateway: Error placing order during shutdown:"},
        ShutdownExceptionParam{
            []() { return std::make_unique<WildThrowingRestClient>(); },
            "OrderGateway: Unknown error placing order during shutdown"}));
