#pragma once

#include <cstdint>

#pragma pack(push, 1)
struct MarketEvent {
    uint8_t eventType;
    char symbol[7];
    uint64_t timestamp;
    uint32_t p1; // Bid or Trade price
    uint32_t s1; // Bid or Trade size
    uint32_t p2; // Ask or Trade price
    uint32_t s2; // Ask or Trade size
    uint64_t arrivedAt;
};
#pragma pack(pop)

static_assert(sizeof(MarketEvent) == 40, "Struct size mismatch");
