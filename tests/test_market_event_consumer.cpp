#include "MarketEvent.hpp"
#include "MarketEventConsumer.hpp"
#include "Order.hpp"
#include "Strategy.hpp"
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <thread>
#include <unistd.h>

struct ThreadGuard {
    std::thread t;
    ThreadGuard(std::thread &&thread) : t(std::move(thread)) {}
    ~ThreadGuard() {
        if (t.joinable()) {
            t.join();
        }
    }
    ThreadGuard(const ThreadGuard &) = delete;
    ThreadGuard &operator=(const ThreadGuard &) = delete;
};

class MockStrategy : public Strategy {
  public:
    std::vector<Order> onMarketEvent(const MarketEvent &event) {
        std::vector<Order> orders;
        if (event.eventType == 2) {
            Order buy_order{};
            buy_order.id = 1;
            buy_order.side = OrderSide::Buy;
            buy_order.type = OrderType::Market;
            buy_order.quantity = 100;
            orders.push_back(buy_order);
        }
        return orders;
    }
};

class MarketEventConsumerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ipc_address =
            "ipc:///tmp/test_market_data_" + std::to_string(getpid()) + ".sock";
    }

    std::string ipc_address;
};

TEST_F(MarketEventConsumerTest, CallsStrategyAndReceivesOrders) {
    std::atomic<bool> is_test_running(true);
    std::promise<std::vector<Order>> promise;
    auto future = promise.get_future();
    auto test_callback = [&](const std::vector<Order> &orders) {
        promise.set_value(orders);
    };
    MarketEventConsumer<MockStrategy> consumer(ipc_address, "TEST",
                                               is_test_running, test_callback);
    ThreadGuard consumer_thread_guard{
        std::thread(&MarketEventConsumer<MockStrategy>::run, &consumer)};
    zmq::context_t context(1);
    zmq::socket_t publisher(context, zmq::socket_type::pub);
    publisher.bind(ipc_address);
    std::this_thread::sleep_for(
        std::chrono::milliseconds(100)); // Allow time for bind
    MarketEvent sent_event{};
    sent_event.eventType = 2; // Trade event

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
    std::vector<Order> received_orders = future.get();
    ASSERT_EQ(received_orders.size(), 1);
    EXPECT_EQ(received_orders[0].id, 1);
    EXPECT_EQ(received_orders[0].side, OrderSide::Buy);
    EXPECT_EQ(received_orders[0].quantity, 100);
}
