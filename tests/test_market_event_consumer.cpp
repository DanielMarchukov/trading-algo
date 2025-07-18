#include "MarketEvent.hpp"
#include "MarketEventConsumer.hpp"
#include "Order.hpp"
#include "Strategy.hpp"
#include <chrono>
#include <filesystem>
#include <future>
#include <gtest/gtest.h>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

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
#ifdef _WIN32
        ipc_address = "tcp://127.0.0.1:5555";
#else
        std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
        std::filesystem::path socket_path =
            temp_dir / ("test_market_data.sock");
        ipc_address = "ipc:///" + socket_path.string();
#endif
        risk_manager_ =
            std::make_shared<RiskManager>(std::make_shared<PositionManager>());
    }

    std::string ipc_address;
    std::shared_ptr<RiskManager> risk_manager_;
};

TEST_F(MarketEventConsumerTest, CallsStrategyAndReceivesOrders) {
    zmq::context_t context(1);
    zmq::socket_t publisher(context, zmq::socket_type::pub);
    publisher.bind(ipc_address);

    std::atomic<bool> is_test_running(true);
    std::promise<Order> promise;
    auto future = promise.get_future();
    auto test_callback = [&](const Order &order) { promise.set_value(order); };
    MarketEventConsumer<MockStrategy> consumer(context, ipc_address, "TEST",
                                               is_test_running, test_callback,
                                               risk_manager_);

    ThreadGuard consumer_thread_guard{
        std::thread(&MarketEventConsumer<MockStrategy>::run, &consumer)};

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
