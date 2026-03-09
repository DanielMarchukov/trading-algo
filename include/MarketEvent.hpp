#pragma once

#include <cstdint>
#include <type_traits>

struct alignas(64) MarketEvent {
  uint64_t eventType;
  char symbol[8];
  uint64_t timestamp;
  uint64_t p1;
  uint64_t s1;
  uint64_t p2;
  uint64_t s2;
  uint64_t arrivedAt;
};

static_assert(sizeof(MarketEvent) == 64, "Struct size mismatch");
static_assert(alignof(MarketEvent) == 64,
              "MarketEvent must be cache-line aligned");
static_assert(std::is_trivially_copyable_v<MarketEvent>,
              "MarketEvent must be trivially copyable for lock-free queues");
