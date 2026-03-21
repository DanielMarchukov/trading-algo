#pragma once

#include "Fill.hpp"
#include "Order.hpp"
#include <algorithm>
#include <cstring>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

namespace test_helpers {

/// Replace a named logger with a ringbuffer sink for test assertions.
/// The global test environment (TestLoggingSetup.cpp) must have run first.
[[nodiscard]] inline auto installTestSink(const char *logger_name,
                                          size_t capacity = 128) {
  auto sink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(capacity);
  auto logger = std::make_shared<spdlog::logger>(logger_name, sink);
  logger->set_level(spdlog::level::trace);
  spdlog::drop(logger_name);
  spdlog::register_logger(logger);
  return sink;
}

/// Check if any formatted log message in the sink contains the given substring.
[[nodiscard]] inline bool
sinkContains(const std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> &sink,
             const std::string &substr) {
  for (const auto &msg : sink->last_formatted()) {
    if (msg.find(substr) != std::string::npos) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] inline Order createOrder(const char *symbol, const OrderSide side,
                                       const int64_t qty,
                                       const uint64_t price) {
  Order order{};
  std::memset(order.symbol, 0, sizeof(order.symbol));
  const auto len = (std::min)(std::strlen(symbol), sizeof(order.symbol));
  std::memcpy(order.symbol, symbol, len);
  order.side = side;
  order.quantity = qty;
  order.price = price;
  return order;
}

[[nodiscard]] inline Fill createFill(const char *symbol, const OrderSide side,
                                     const int64_t qty) {
  Fill fill{};
  std::memset(fill.symbol, 0, sizeof(fill.symbol));
  const auto len = (std::min)(std::strlen(symbol), sizeof(fill.symbol));
  std::memcpy(fill.symbol, symbol, len);
  fill.side = side;
  fill.quantity = qty;
  return fill;
}

} // namespace test_helpers
