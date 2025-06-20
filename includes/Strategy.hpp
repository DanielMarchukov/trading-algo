#pragma once

#include "MarketEvent.hpp"
#include "Order.hpp"
#include <vector>

class Strategy {
  public:
    ~Strategy() = default;

    std::vector<Order> onMarketEvent(const MarketEvent &event);
};
