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
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(order->side, OrderSide::Buy);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(order->type, OrderType::Limit);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(order->quantity, 100);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(order->price, 1499900);
}

TEST_F(SimpleMarketMakingStrategyTest, LeavesOrderIdUnset) {
  MarketEvent trade{};
  trade.eventType = 2;
  trade.p1 = 1000000;

  const auto order = strategy.onMarketEvent(trade);
  ASSERT_TRUE(order.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(order->id, 0);
}

struct RejectParam {
  uint8_t event_type;
  uint64_t p1;
  const char *name;
};

class StrategyRejectsInputTest : public ::testing::TestWithParam<RejectParam> {
protected:
  SimpleMarketMakingStrategy strategy;
};

TEST_P(StrategyRejectsInputTest, ReturnsNullopt) {
  const auto &[event_type, p1, name] = GetParam();
  MarketEvent event{};
  event.eventType = event_type;
  event.p1 = p1;

  EXPECT_FALSE(strategy.onMarketEvent(event).has_value()) << name;
}

INSTANTIATE_TEST_SUITE_P(
    InvalidInputs, StrategyRejectsInputTest,
    ::testing::Values(RejectParam{1, 1500000, "quote event (non-trade)"},
                      RejectParam{2, 0, "zero price"},
                      RejectParam{2, 50, "price below offset"}));
