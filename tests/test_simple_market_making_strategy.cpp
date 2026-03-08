#include "MarketEvent.hpp"
#include "Order.hpp"
#include "SimpleMarketMakingStrategy.hpp"
#include <gtest/gtest.h>

class SimpleMarketMakingStrategyTest : public ::testing::Test {
protected:
  SimpleMarketMakingStrategy strategy;
};

TEST_F(SimpleMarketMakingStrategyTest, GeneratesBuyOrderOnTradeEvent) {
  MarketEvent trade_event{};
  trade_event.eventType = 2;
  trade_event.p1 = 1500000;

  const auto order = strategy.onMarketEvent(trade_event);

  ASSERT_TRUE(order.has_value());
  EXPECT_EQ(order->side, OrderSide::Buy);
  EXPECT_EQ(order->type, OrderType::Limit);
  EXPECT_EQ(order->quantity, 100);
  EXPECT_EQ(order->price, 1499900);
}

TEST_F(SimpleMarketMakingStrategyTest, IgnoresQuoteEvent) {
  MarketEvent quote_event{};
  quote_event.eventType = 1;
  quote_event.p1 = 1500000;
  quote_event.p2 = 1500200;

  EXPECT_FALSE(strategy.onMarketEvent(quote_event).has_value());
}

TEST_F(SimpleMarketMakingStrategyTest, LeavesOrderIdUnset) {
  MarketEvent trade{};
  trade.eventType = 2;
  trade.p1 = 1000000;

  const auto order = strategy.onMarketEvent(trade);
  ASSERT_TRUE(order.has_value());
  EXPECT_EQ(order->id, 0);
}

TEST_F(SimpleMarketMakingStrategyTest, HandlesZeroPrice) {
  MarketEvent trade_event{};
  trade_event.eventType = 2;
  trade_event.p1 = 0;

  EXPECT_FALSE(strategy.onMarketEvent(trade_event).has_value());
}

TEST_F(SimpleMarketMakingStrategyTest, SkipsOrderWhenBuyPriceUnderflows) {
  MarketEvent trade_event{};
  trade_event.eventType = 2;
  trade_event.p1 = 50;

  EXPECT_FALSE(strategy.onMarketEvent(trade_event).has_value());
}
