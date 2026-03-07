#pragma once

#include "MarketEvent.hpp"
#include "Order.hpp"
#include <concepts>

template <typename T>
concept StrategyLike = requires(T t, const MarketEvent &e) {
  { t.onMarketEvent(e) } -> std::same_as<OrderBatch>;
};
