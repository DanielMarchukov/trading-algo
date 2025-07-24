#include "MarketEvent.hpp"
#include "Order.hpp"
#include "SimpleMarketMakingStrategy.hpp"
#include <gtest/gtest.h>
#include <vector>

class SimpleMarketMakingStrategyTest : public ::testing::Test {
protected:
  SimpleMarketMakingStrategy strategy;
};

TEST_F(SimpleMarketMakingStrategyTest, GeneratesOrdersOnTradeEvent) {
  MarketEvent trade_event{};
  trade_event.eventType = 2;
  trade_event.p1 = 1500000;

  const std::vector<Order> generated_orders =
      strategy.onMarketEvent(trade_event);

  ASSERT_EQ(generated_orders.size(), 2);
  const auto &buy_order = generated_orders[0];
  const auto &sell_order = generated_orders[1];
  EXPECT_EQ(buy_order.side, OrderSide::Buy);
  EXPECT_EQ(buy_order.type, OrderType::Limit);
  EXPECT_EQ(buy_order.quantity, 100);
  EXPECT_EQ(buy_order.price, 1499900);
  EXPECT_EQ(sell_order.side, OrderSide::Sell);
  EXPECT_EQ(sell_order.type, OrderType::Limit);
  EXPECT_EQ(sell_order.quantity, 100);
  EXPECT_EQ(sell_order.price, 1500100);
}

TEST_F(SimpleMarketMakingStrategyTest, IgnoresQuoteEvent) {
  MarketEvent quote_event{};
  quote_event.eventType = 1;
  quote_event.p1 = 1500000;
  quote_event.p2 = 1500200;

  const std::vector<Order> generated_orders =
      strategy.onMarketEvent(quote_event);

  EXPECT_TRUE(generated_orders.empty());
}

TEST_F(SimpleMarketMakingStrategyTest, OrderIdIncrements) {
  MarketEvent trade1{};
  trade1.eventType = 2;
  trade1.p1 = 1000000;

  MarketEvent trade2{};
  trade2.eventType = 2;
  trade2.p1 = 1010000;

  const std::vector<Order> orders1 = strategy.onMarketEvent(trade1);
  ASSERT_EQ(orders1.size(), 2);
  EXPECT_EQ(orders1[0].id, 1);
  EXPECT_EQ(orders1[1].id, 2);

  const std::vector<Order> orders2 = strategy.onMarketEvent(trade2);
  ASSERT_EQ(orders2.size(), 2);
  EXPECT_EQ(orders2[0].id, 3);
  EXPECT_EQ(orders2[1].id, 4);
}

TEST_F(SimpleMarketMakingStrategyTest, HandlesZeroPrice) {
  MarketEvent trade_event{};
  trade_event.eventType = 2;
  trade_event.p1 = 0;

  const auto orders = strategy.onMarketEvent(trade_event);
  EXPECT_TRUE(orders.empty());
}
