#pragma once

#include <cstdint>
#include <type_traits>

enum class OrderSide : uint8_t { Buy, Sell };

enum class OrderType : uint8_t { Market, Limit };

constexpr int64_t SCALING_FACTOR = 10000;

struct alignas(64) Order {
  uint64_t id;
  char symbol[8];
  int64_t quantity;
  uint64_t price;
  OrderSide side;
  OrderType type;
};

static_assert(sizeof(Order) == 64, "Order must be exactly one cache line");
static_assert(alignof(Order) == 64, "Order must be cache-line aligned");
static_assert(std::is_trivially_copyable_v<Order>,
              "Order must be trivially copyable for lock-free queues");
