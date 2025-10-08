#pragma once

#include <cstdint>

enum class OrderSide : uint8_t { Buy, Sell };

enum class OrderType : uint8_t { Market, Limit };

constexpr int64_t SCALING_FACTOR = 10000;

struct Order {
  uint64_t id;
  char symbol[8];
  int64_t quantity;
  uint64_t price;
  OrderSide side;
  OrderType type;
};

static_assert(sizeof(Order) == 40, "Expected size is 40 bytes");
