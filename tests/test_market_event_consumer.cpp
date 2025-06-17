#include "MarketEvent.hpp"
#include "MarketEventConsumer.hpp"
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <thread>

class MarketEventConsumerTest : public ::testing::Test {
  protected:
    void SetUp() override { g_is_running.store(true); }

    void TearDown() override { g_is_running.store(false); }

    const std::string ipc_address = "ipc:///tmp/test_market_data.sock";
    std::atomic<bool> g_is_running{true};
};

TEST_F(MarketEventConsumerTest, ReceivesAndProcessesCorrectMessage) {
    std::promise<MarketEvent> promise;
    auto future = promise.get_future();
    auto test_callback = [&](const MarketEvent &event) {
        promise.set_value(event);
    };
    MarketEventConsumer consumer(ipc_address, "TEST", g_is_running,
                                 test_callback);
    std::thread thread(&MarketEventConsumer::run, &consumer);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    zmq::context_t context(1);
    zmq::socket_t publisher(context, zmq::socket_type::pub);
    publisher.bind(ipc_address);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    MarketEvent sent_event{};
    sent_event.eventType = 1;
    sent_event.timestamp = 123456789;
    sent_event.p1 = 99.99;
    zmq::message_t topic("TEST", 4);
    zmq::message_t payload(&sent_event, sizeof(MarketEvent));

    publisher.send(topic, zmq::send_flags::sndmore);
    publisher.send(payload, zmq::send_flags::none);

    auto status = future.wait_for(std::chrono::milliseconds(100));
    ASSERT_EQ(status, std::future_status::ready);
    MarketEvent received_event = future.get();
    EXPECT_EQ(received_event.eventType, sent_event.eventType);
    EXPECT_EQ(received_event.timestamp, sent_event.timestamp);
    EXPECT_DOUBLE_EQ(received_event.p1, sent_event.p1);

    g_is_running.store(false);
    if (thread.joinable()) {
        thread.join();
    }
}
