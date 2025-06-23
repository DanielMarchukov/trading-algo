#pragma once

#include "Strategy.hpp"
#include <string.h>

class SimpleMarketMakingStrategy : public Strategy {
  public:
    SimpleMarketMakingStrategy()
        : last_trade_price_(0.0), order_id_counter_(0) {}

    std::vector<Order> onMarketEvent(const MarketEvent &event) {
        std::vector<Order> orders;

        if (event.eventType == 2) {
            last_trade_price_ = event.p1;

            if (last_trade_price_ > 0) {
                Order buy_order{};
                buy_order.id = ++order_id_counter_;
                strncpy(buy_order.symbol, event.symbol,
                        sizeof(buy_order.symbol) - 1);
                buy_order.side = OrderSide::Buy;
                buy_order.type = OrderType::Limit;
                buy_order.quantity = 100;
                buy_order.price = last_trade_price_ - 0.01;
                orders.push_back(buy_order);

                Order sell_order{};
                sell_order.id = ++order_id_counter_;
                strncpy(sell_order.symbol, event.symbol,
                        sizeof(sell_order.symbol) - 1);
                sell_order.side = OrderSide::Sell;
                sell_order.type = OrderType::Limit;
                sell_order.quantity = 100;
                sell_order.price = last_trade_price_ + 0.01;
                orders.push_back(sell_order);
            }
        }
        return orders;
    }

  private:
    double last_trade_price_;
    uint64_t order_id_counter_;
};
