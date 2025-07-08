#include "Order.hpp"
#include "OrderGateway.hpp"
#include "ThreadSafeQueue.hpp"
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

class MockAlpacaRestClient {
  public:
    MockAlpacaRestClient(std::promise<void> *p = nullptr)
        : promise_to_fulfill(p) {}

    void placeOrder(const Order &_) {
        if (promise_to_fulfill) {
            promise_to_fulfill->set_value();
        }
    }

    std::promise<void> *promise_to_fulfill = nullptr;
};

struct ThreadGuard {
    std::thread t;
    ThreadGuard(std::thread &&thread) : t(std::move(thread)) {}
    ~ThreadGuard() {
        if (t.joinable())
            t.join();
    }
};

TEST(OrderGatewayTest, ProcessesOrderAndCallsRestClient) {
    std::atomic<bool> is_running(true);
    auto order_queue = std::make_shared<ThreadSafeQueue<Order>>();
    std::promise<void> promise;
    auto future = promise.get_future();
    auto mock_client = std::make_unique<MockAlpacaRestClient>(&promise);
    OrderGateway<MockAlpacaRestClient> gateway(is_running, order_queue,
                                               std::move(mock_client));
    ThreadGuard gateway_thread_guard{
        std::thread(&OrderGateway<MockAlpacaRestClient>::run, &gateway)};

    Order test_order{};
    test_order.id = 999;
    order_queue->push(test_order);
    auto status = future.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);

    is_running.store(false);
}
