#pragma once

#include "MarketEvent.hpp"
#include "Order.hpp"
#include "Utils.hpp"

#include <cstring>
#include <optional>

class SimpleMarketMakingStrategy {
public:
  explicit SimpleMarketMakingStrategy(uint64_t spread_offset_ticks = 100,
                                      int64_t order_quantity = 100)
      : last_trade_price_(0), spread_offset_ticks_(spread_offset_ticks),
        order_quantity_(order_quantity) {}

  [[nodiscard]] std::optional<Order>
  onMarketEvent(const MarketEvent &event) noexcept {
    if (event.eventType != 2) [[unlikely]] {
      return std::nullopt;
    }

    last_trade_price_ = event.p1;

    if (last_trade_price_ < spread_offset_ticks_) [[unlikely]] {
      return std::nullopt;
    }

    Order order{};
    std::memcpy(order.symbol, event.symbol, sizeof(order.symbol));
    order.side = OrderSide::Buy;
    order.type = OrderType::Limit;
    order.quantity = order_quantity_;
    order.price = last_trade_price_ - spread_offset_ticks_;
    return order;
  }

private:
  uint64_t last_trade_price_;
  uint64_t spread_offset_ticks_;
  int64_t order_quantity_;
};
