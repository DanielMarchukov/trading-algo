#pragma once
#include "Order.hpp" // We can reuse the OrderSide enum
#include <cstdint>

struct Fill {
  char symbol[8];
  uint64_t executionId;
  uint64_t orderId;
  OrderSide side;
  int64_t quantity;
  double price;
};
