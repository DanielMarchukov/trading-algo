#include "MarketEvent.hpp"
#include "ZmqMarketEventSink.hpp"
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <zmq.hpp>

class ZmqSinkTest : public ::testing::Test {
protected:
  void SetUp() override {
    zmq_address_ = "inproc://test_sink_" +
                   std::to_string(reinterpret_cast<uintptr_t>(this));

    sink_ = std::make_unique<ZmqMarketEventSink>(context_, zmq_address_);
    sink_->start();

    sub_ = zmq::socket_t(context_, zmq::socket_type::sub);
    sub_.set(zmq::sockopt::rcvtimeo, 500);
    sub_.set(zmq::sockopt::subscribe, "");
    sub_.connect(zmq_address_);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  void TearDown() override {
    sub_.close();
    sink_.reset();
  }

  [[nodiscard]] bool recvEvent(MarketEvent &event, std::string &topic_out) {
    zmq::message_t topic;
    zmq::message_t payload;
    if (auto r = sub_.recv(topic, zmq::recv_flags::none); !r.has_value()) {
      return false;
    }
    if (auto r = sub_.recv(payload, zmq::recv_flags::none); !r.has_value()) {
      return false;
    }
    if (payload.size() != sizeof(MarketEvent)) {
      return false;
    }
    topic_out = std::string(static_cast<char *>(topic.data()), topic.size());
    std::memcpy(&event, payload.data(), sizeof(MarketEvent));
    return true;
  }

  zmq::context_t context_{1};
  std::string zmq_address_;
  std::unique_ptr<ZmqMarketEventSink> sink_;
  zmq::socket_t sub_;
};

TEST_F(ZmqSinkTest, PublishesQuoteEvent) {
  MarketEvent event{};
  event.eventType = 1;
  std::memset(event.symbol, 0, 8);
  std::memcpy(event.symbol, "AAPL", 4);
  event.p1 = 1502500;
  event.s1 = 200;
  event.p2 = 1503000;
  event.s2 = 100;
  event.timestamp = 12345;
  event.arrivedAt = 67890;

  sink_->publish(event, "AAPL");

  MarketEvent received{};
  std::string topic;
  ASSERT_TRUE(recvEvent(received, topic));

  EXPECT_EQ(topic, "AAPL");
  EXPECT_EQ(received.eventType, 1ULL);
  EXPECT_EQ(received.p1, 1502500ULL);
  EXPECT_EQ(received.s1, 200ULL);
  EXPECT_EQ(received.p2, 1503000ULL);
  EXPECT_EQ(received.s2, 100ULL);
}

TEST_F(ZmqSinkTest, PublishesTradeEvent) {
  MarketEvent event{};
  event.eventType = 2;
  std::memset(event.symbol, 0, 8);
  std::memcpy(event.symbol, "GOOGL", 5);
  event.p1 = 28005000;
  event.s1 = 50;

  sink_->publish(event, "GOOGL");

  MarketEvent received{};
  std::string topic;
  ASSERT_TRUE(recvEvent(received, topic));

  EXPECT_EQ(topic, "GOOGL");
  EXPECT_EQ(received.eventType, 2ULL);
  EXPECT_EQ(received.p1, 28005000ULL);
  EXPECT_EQ(received.s1, 50ULL);
}

TEST_F(ZmqSinkTest, TopicTruncatedTo8Chars) {
  MarketEvent event{};
  event.eventType = 2;
  std::memset(event.symbol, 0, 8);
  std::memcpy(event.symbol, "LONGERSY", 8);

  sink_->publish(event, "LONGERSYMBOL");

  MarketEvent received{};
  std::string topic;
  ASSERT_TRUE(recvEvent(received, topic));

  EXPECT_EQ(topic, "LONGERSY");
}

TEST_F(ZmqSinkTest, MultiplePublishesInSequence) {
  MarketEvent e1{};
  e1.eventType = 1;
  std::memcpy(e1.symbol, "AAPL", 4);
  sink_->publish(e1, "AAPL");

  MarketEvent e2{};
  e2.eventType = 2;
  std::memcpy(e2.symbol, "TSLA", 4);
  sink_->publish(e2, "TSLA");

  MarketEvent r1{};
  std::string t1;
  ASSERT_TRUE(recvEvent(r1, t1));
  EXPECT_EQ(t1, "AAPL");
  EXPECT_EQ(r1.eventType, 1ULL);

  MarketEvent r2{};
  std::string t2;
  ASSERT_TRUE(recvEvent(r2, t2));
  EXPECT_EQ(t2, "TSLA");
  EXPECT_EQ(r2.eventType, 2ULL);
}

TEST_F(ZmqSinkTest, PublishesWithEmptySymbol) {
  MarketEvent event{};
  event.eventType = 1;

  sink_->publish(event, "");

  MarketEvent received{};
  std::string topic;
  ASSERT_TRUE(recvEvent(received, topic));
  EXPECT_TRUE(topic.empty());
  EXPECT_EQ(received.eventType, 1ULL);
}

TEST_F(ZmqSinkTest, StopIsIdempotent) {
  sink_->stop();
  EXPECT_NO_THROW(sink_->stop());
}
