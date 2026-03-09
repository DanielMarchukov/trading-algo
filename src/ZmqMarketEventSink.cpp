#include "ZmqMarketEventSink.hpp"
#include <algorithm>
#include <iostream>

namespace {
constexpr std::size_t kSymbolCapacity = 8;
} // namespace

ZmqMarketEventSink::ZmqMarketEventSink(zmq::context_t &context,
                                       std::string zmq_address)
    : zmq_address_(std::move(zmq_address)),
      zmq_pub_(context, zmq::socket_type::pub) {}

void ZmqMarketEventSink::start() {
  zmq_pub_.set(zmq::sockopt::sndhwm, 1000000);
  zmq_pub_.set(zmq::sockopt::linger, 0);
  zmq_pub_.bind(zmq_address_);
  std::cout << "ZmqMarketEventSink: bound to " << zmq_address_ << std::endl;
}

void ZmqMarketEventSink::stop() {}

void ZmqMarketEventSink::publish(const MarketEvent &event,
                                 std::string_view symbol) {
  const auto topic_len = (std::min)(symbol.size(), kSymbolCapacity);
  auto res =
      zmq_pub_.send(zmq::buffer(symbol.data(), topic_len),
                    zmq::send_flags::sndmore | zmq::send_flags::dontwait);
  if (!res.has_value()) {
    return;
  }
  zmq_pub_.send(zmq::buffer(&event, sizeof(MarketEvent)),
                zmq::send_flags::dontwait);
}
