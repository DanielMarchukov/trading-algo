#pragma once
#include "Order.hpp"
#include <cstdint>
#include <type_traits>

struct Fill {
  char symbol[8];
  uint64_t executionId;
  uint64_t orderId;
  OrderSide side;
  int64_t quantity;
  double price;
};

static_assert(sizeof(Fill) == 48, "Fill must be 48 bytes");
static_assert(alignof(Fill) == 8, "Fill must be 8-byte aligned");
static_assert(std::is_trivially_copyable_v<Fill>,
              "Fill must be trivially copyable for lock-free queues");
