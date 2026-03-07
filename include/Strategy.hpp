#pragma once

#include "MarketEvent.hpp"
#include "Order.hpp"
#include <concepts>
#include <vector>

template <typename T>
concept StrategyLike = requires(T t, const MarketEvent &e) {
  { t.onMarketEvent(e) } -> std::same_as<std::vector<Order>>;
};
