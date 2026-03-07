#pragma once

#include "MarketEvent.hpp"
#include "Order.hpp"
#include <concepts>
#include <optional>

template <typename T>
concept StrategyLike = requires(T t, const MarketEvent &e) {
  { t.onMarketEvent(e) } -> std::same_as<std::optional<Order>>;
};
