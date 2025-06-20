#ifndef MARKET_EVENT_HPP
#define MARKET_EVENT_HPP

#include <cstdint>

#pragma pack(push, 1)
struct MarketEvent {
    uint8_t eventType;
    char symbol[7];
    uint64_t timestamp;
    double p1;   // Bid or Trade price
    uint32_t s1; // Bid or Trade size
    double p2;
    uint32_t s2;
    uint64_t arrivedAt;
};
#pragma pack(pop)

static_assert(sizeof(MarketEvent) == 48, "Struct size mismatch");

#endif
