#include "IRestClient.hpp"
#include "MarketEvent.hpp"
#include "Order.hpp"
#include "TradingEngine.hpp"
#include <filesystem>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>
#include <zmq.hpp>

class MockRestClient : public IRestClient {
  public:
    explicit MockRestClient(std::promise<Order> *p) : promise_to_fulfill(p) {}

    void placeOrder(const Order &order) override {
        if (promise_to_fulfill) {
            promise_to_fulfill->set_value(order);
        }
    }

    std::promise<Order> *promise_to_fulfill = nullptr;
};

struct EngineTestGuard {
    TradingEngine &engine;
    std::thread &engine_thread;
    ~EngineTestGuard() {
        engine.stop();
        if (engine_thread.joinable()) {
            engine_thread.join();
        }
    }
};

TEST(TradingEngineIntegrationTest, FullFlowFromMarketEventToOrder) {
#ifndef _WIN32
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
    std::filesystem::path socket_path = temp_dir / "market_data.sock";
#endif

    std::vector<std::string> symbols = {"TEST_SYM"};
    std::promise<Order> order_promise;
    auto order_future = order_promise.get_future();

    auto mock_client = std::make_unique<MockRestClient>(&order_promise);
    TradingEngine engine(symbols, std::move(mock_client));

    zmq::socket_t publisher(engine.getContext(), zmq::socket_type::pub);
    publisher.bind(engine.getIPCAddress());

    std::thread engine_thread(&TradingEngine::run, &engine);
    EngineTestGuard guard{engine, engine_thread};

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    MarketEvent sent_event{};
    sent_event.eventType = 2;

    auto start_time = std::chrono::steady_clock::now();
    while (order_future.wait_for(std::chrono::milliseconds(10)) !=
           std::future_status::ready) {
        zmq::message_t topic(symbols[0].c_str(), symbols[0].length());
        zmq::message_t payload(&sent_event, sizeof(MarketEvent));
        publisher.send(topic, zmq::send_flags::sndmore);
        publisher.send(payload, zmq::send_flags::none);
        if (std::chrono::steady_clock::now() - start_time >
            std::chrono::seconds(2)) {
            break;
        }
    }

    ASSERT_EQ(order_future.wait_for(std::chrono::seconds(0)),
              std::future_status::ready);

    Order received_order = order_future.get();
    EXPECT_EQ(received_order.id, 1);
    EXPECT_EQ(received_order.side, OrderSide::Buy);
    EXPECT_EQ(received_order.quantity, 100);
}
