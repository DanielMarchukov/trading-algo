#pragma once

#include <cstdint>

enum class OrderSide : uint8_t { Buy, Sell };

enum class OrderType : uint8_t { Market, Limit };

constexpr int16_t SCALING_FACTOR = 10000;

#pragma pack(push, 1)
struct Order {
  uint64_t id;
  char symbol[8];
  OrderSide side;
  OrderType type;
  int32_t quantity;
  uint32_t price;
};
#pragma pack(pop)

static_assert(sizeof(Order) == 26, "Expected size is 26 bytes");
