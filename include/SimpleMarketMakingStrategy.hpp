#pragma once

#include "MarketEvent.hpp"
#include "Order.hpp"
#include "Utils.hpp"

#include <cstring>
#include <limits>
#include <vector>

class SimpleMarketMakingStrategy {
public:
  SimpleMarketMakingStrategy() : last_trade_price_(0), order_id_counter_(0) {}

  std::vector<Order> onMarketEvent(const MarketEvent &event) {
    std::vector<Order> orders;

    if (event.eventType == 2) {
      last_trade_price_ = event.p1;

      constexpr uint64_t offset_ticks = 100;
      if (last_trade_price_ == 0) {
        return orders;
      }

      const uint64_t max_price = (std::numeric_limits<uint64_t>::max)();
      if (last_trade_price_ < offset_ticks ||
          last_trade_price_ > max_price - offset_ticks) {
        return orders;
      }

      Order buy_order{};
      buy_order.id = ++order_id_counter_;
      std::memcpy(buy_order.symbol, event.symbol, sizeof(buy_order.symbol));
      buy_order.side = OrderSide::Buy;
      buy_order.type = OrderType::Limit;
      buy_order.quantity = 100;
      buy_order.price = last_trade_price_ - offset_ticks;
      orders.push_back(buy_order);

      Order sell_order{};
      sell_order.id = ++order_id_counter_;
      std::memcpy(sell_order.symbol, event.symbol, sizeof(sell_order.symbol));
      sell_order.side = OrderSide::Sell;
      sell_order.type = OrderType::Limit;
      sell_order.quantity = 100;
      sell_order.price = last_trade_price_ + offset_ticks;
      orders.push_back(sell_order);
    }
    return orders;
  }

private:
  uint64_t last_trade_price_;
  uint64_t order_id_counter_;
};
