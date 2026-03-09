#pragma once

#include "MarketDataPipeline.hpp"
#include "MarketEvent.hpp"
#include <string>
#include <string_view>
#include <zmq.hpp>

class ZmqMarketEventSink {
public:
  ZmqMarketEventSink(zmq::context_t &context, std::string zmq_address);
  ~ZmqMarketEventSink() = default;

  ZmqMarketEventSink(ZmqMarketEventSink &&) = default;
  ZmqMarketEventSink &operator=(ZmqMarketEventSink &&) = delete;
  ZmqMarketEventSink(const ZmqMarketEventSink &) = delete;
  ZmqMarketEventSink &operator=(const ZmqMarketEventSink &) = delete;

  void start();
  void stop();
  void publish(const MarketEvent &event, std::string_view symbol);

private:
  std::string zmq_address_;
  zmq::socket_t zmq_pub_;
};

static_assert(MarketEventSinkLike<ZmqMarketEventSink>,
              "ZmqMarketEventSink must satisfy MarketEventSinkLike");
