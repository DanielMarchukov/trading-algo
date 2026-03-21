#include "ZmqMarketEventSink.hpp"
#include <algorithm>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace {
constexpr std::size_t kSymbolCapacity = 8;
constexpr std::string_view kIpcPrefix = "ipc://";
} // namespace

ZmqMarketEventSink::ZmqMarketEventSink(zmq::context_t &context,
                                       std::string zmq_address)
    : zmq_address_(std::move(zmq_address)),
      zmq_pub_(context, zmq::socket_type::pub) {}

void ZmqMarketEventSink::start() {
  if (zmq_address_.starts_with(kIpcPrefix)) {
    auto sock_path = zmq_address_.substr(kIpcPrefix.size());
    std::error_code ec;
    std::filesystem::remove(sock_path, ec);
  }
  zmq_pub_.set(zmq::sockopt::sndhwm, 1000000);
  zmq_pub_.set(zmq::sockopt::linger, 0);
  zmq_pub_.bind(zmq_address_);
  spdlog::get("zmq")->info("Bound to {}", zmq_address_);
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
  auto res2 = zmq_pub_.send(zmq::buffer(&event, sizeof(MarketEvent)),
                            zmq::send_flags::dontwait);
  (void)res2;
}
