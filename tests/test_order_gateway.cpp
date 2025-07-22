#include "IRestClient.hpp"
#include "Order.hpp"
#include "OrderGateway.hpp"
#include "ThreadGuard.hpp"
#include "ThreadSafeQueue.hpp"
#include <future>
#include <gtest/gtest.h>
#include <memory>
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
