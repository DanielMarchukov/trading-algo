#pragma once

#include "MarketEvent.hpp"
#include "Order.hpp"
#include "Utils.hpp"

#include <cstring>
#include <optional>

class SimpleMarketMakingStrategy {
public:
  SimpleMarketMakingStrategy() : last_trade_price_(0), order_id_counter_(0) {}

  std::optional<Order> onMarketEvent(const MarketEvent &event) {
    if (event.eventType != 2) {
      return std::nullopt;
    }

    last_trade_price_ = event.p1;

    constexpr uint64_t offset_ticks = 100;
    if (last_trade_price_ < offset_ticks) {
      return std::nullopt;
    }

    Order order{};
    order.id = ++order_id_counter_;
    std::memcpy(order.symbol, event.symbol, sizeof(order.symbol));
    order.side = OrderSide::Buy;
    order.type = OrderType::Limit;
    order.quantity = 100;
    order.price = last_trade_price_ - offset_ticks;
    return order;
  }

private:
  uint64_t last_trade_price_;
  uint64_t order_id_counter_;
};
