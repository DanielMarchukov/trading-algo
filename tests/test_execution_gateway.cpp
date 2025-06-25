#include "ExecutionGateway.hpp"
#include "IAlpacaRestClient.hpp"
#include "Order.hpp"
#include "PositionManager.hpp"
#include "ThreadSafeQueue.hpp"
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

class MockAlpacaRestClient : public IAlpacaRestClient {
  public:
    MockAlpacaRestClient(std::promise<void> *p = nullptr)
        : promise_to_fulfill(p) {}

    bool placeOrder(const Order &_) override {
        if (promise_to_fulfill) {
            promise_to_fulfill->set_value();
        }
        return should_succeed;
    }

    bool should_succeed = true;
    std::promise<void> *promise_to_fulfill = nullptr;
};

struct ThreadGuard {
    std::thread t;
    ThreadGuard(std::thread &&thread) : t(std::move(thread)) {}
    ~ThreadGuard() {
        if (t.joinable())
            t.join();
    }
    ThreadGuard(const ThreadGuard &) = delete;
    ThreadGuard &operator=(const ThreadGuard &) = delete;
};

TEST(ExecutionGatewayTest, ProcessesOrderAndUpdatesPositionOnSuccess) {
    std::atomic<bool> is_running(true);
    auto order_queue = std::make_shared<ThreadSafeQueue<Order>>();
    auto pos_manager = std::make_shared<PositionManager>();
    std::promise<void> order_processed_promise;
    auto order_processed_future = order_processed_promise.get_future();
    auto mock_client =
        std::make_unique<MockAlpacaRestClient>(&order_processed_promise);
    mock_client->should_succeed = true;
    ExecutionGateway gateway(is_running, order_queue, std::move(mock_client),
                             pos_manager);
    ThreadGuard gateway_thread_guard{
        std::thread(&ExecutionGateway::run, &gateway)};

    Order test_order{};
    strncpy(test_order.symbol, "GOOGL", sizeof(test_order.symbol));
    test_order.side = OrderSide::Buy;
    test_order.quantity = 50;
    order_queue->push(test_order);

    auto status = order_processed_future.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    EXPECT_EQ(pos_manager->getPosition("GOOGL"), 50);

    is_running.store(false);
}
