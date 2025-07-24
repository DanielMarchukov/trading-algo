#include "Fill.hpp"
#include "PositionManager.hpp"
#include "Utils.hpp"
#include <gtest/gtest.h>

class PositionManagerTest : public ::testing::Test {
protected:
  PositionManager pm;
};

TEST_F(PositionManagerTest, NewPositionIsZero) {
  EXPECT_EQ(pm.getPosition("AAPL"), 0);
  EXPECT_EQ(pm.getPosition("GOOGL"), 0);
}

TEST_F(PositionManagerTest, ProcessBuyFill) {
  Fill buy_fill{};
  strncpy(buy_fill.symbol, "AAPL", sizeof(buy_fill.symbol));
  buy_fill.side = OrderSide::Buy;
  buy_fill.quantity = 100;

  pm.onFill(buy_fill);

  EXPECT_EQ(pm.getPosition("AAPL"), 100);
}

TEST_F(PositionManagerTest, ProcessSellFill) {
  Fill sell_fill{};
  strncpy(sell_fill.symbol, "AAPL", sizeof(sell_fill.symbol));
  sell_fill.side = OrderSide::Sell;
  sell_fill.quantity = 75;

  pm.onFill(sell_fill);

  EXPECT_EQ(pm.getPosition("AAPL"), -75);
}

TEST_F(PositionManagerTest, HandlesMultipleSymbolsAndFills) {
  Fill aapl_buy{};
  strncpy(aapl_buy.symbol, "AAPL", sizeof(aapl_buy.symbol));
  aapl_buy.side = OrderSide::Buy;
  aapl_buy.quantity = 200;

  Fill googl_buy{};
  strncpy(googl_buy.symbol, "GOOGL", sizeof(googl_buy.symbol));
  googl_buy.side = OrderSide::Buy;
  googl_buy.quantity = 50;

  Fill aapl_sell{};
  strncpy(aapl_sell.symbol, "AAPL", sizeof(aapl_sell.symbol));
  aapl_sell.side = OrderSide::Sell;
  aapl_sell.quantity = 50;

  pm.onFill(aapl_buy);
  pm.onFill(googl_buy);
  pm.onFill(aapl_sell);

  EXPECT_EQ(pm.getPosition("AAPL"), 150); // 200 - 50
  EXPECT_EQ(pm.getPosition("GOOGL"), 50);
}

TEST_F(PositionManagerTest, HandlesMaxLengthSymbol) {
  Fill fill{};
  memset(fill.symbol, 'X', sizeof(fill.symbol) - 1);
  fill.symbol[sizeof(fill.symbol) - 1] = '\0';
  fill.side = OrderSide::Buy;
  fill.quantity = 100;

  pm.onFill(fill);

  EXPECT_EQ(pm.getPosition(fill.symbol), 100);
}

TEST_F(PositionManagerTest, HandlesEmptySymbol) {
  Fill fill{};
  fill.symbol[0] = '\0';
  fill.side = OrderSide::Buy;
  fill.quantity = 50;

  pm.onFill(fill);

  EXPECT_EQ(pm.getPosition(""), 50);
}
