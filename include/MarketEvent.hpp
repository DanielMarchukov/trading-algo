#pragma once

#include <cstdint>

struct MarketEvent {
  uint64_t eventType;
  char symbol[8];
  uint64_t timestamp;
  uint64_t p1; // Bid or Trade price (scaled by SCALING_FACTOR)
  uint64_t s1; // Bid or Trade size
  uint64_t p2; // Ask price (scaled)
  uint64_t s2; // Ask size
  uint64_t arrivedAt;
};

static_assert(sizeof(MarketEvent) == 64, "Struct size mismatch");
