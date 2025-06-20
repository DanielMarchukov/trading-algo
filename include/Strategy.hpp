#ifndef STRATEGY_HPP
#define STRATEGY_HPP

#include "MarketEvent.hpp"
#include "Order.hpp"
#include <vector>

class Strategy {
  public:
    ~Strategy() = default;

    std::vector<Order> onMarketEvent(const MarketEvent &event);
};

#endif // STRATEGY_HPP
