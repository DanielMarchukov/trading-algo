#pragma once

#include "Fill.hpp"
#include "Order.hpp"
#include <algorithm>
#include <cstring>

namespace test_helpers {

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
