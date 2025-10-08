#include "Fill.hpp"
#include "PositionManager.hpp"
#include "Utils.hpp"
#include <cstring>
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
  std::memset(buy_fill.symbol, 0, sizeof(buy_fill.symbol));
  std::memcpy(buy_fill.symbol, "AAPL", 4);
  buy_fill.side = OrderSide::Buy;
  buy_fill.quantity = 100;

  pm.onFill(buy_fill);

  EXPECT_EQ(pm.getPosition("AAPL"), 100);
}

TEST_F(PositionManagerTest, ProcessSellFill) {
  Fill sell_fill{};
  std::memset(sell_fill.symbol, 0, sizeof(sell_fill.symbol));
  std::memcpy(sell_fill.symbol, "AAPL", 4);
  sell_fill.side = OrderSide::Sell;
  sell_fill.quantity = 75;

  pm.onFill(sell_fill);

  EXPECT_EQ(pm.getPosition("AAPL"), -75);
}

TEST_F(PositionManagerTest, HandlesMultipleSymbolsAndFills) {
  Fill aapl_buy{};
  std::memset(aapl_buy.symbol, 0, sizeof(aapl_buy.symbol));
  std::memcpy(aapl_buy.symbol, "AAPL", 4);
  aapl_buy.side = OrderSide::Buy;
  aapl_buy.quantity = 200;

  Fill googl_buy{};
  std::memset(googl_buy.symbol, 0, sizeof(googl_buy.symbol));
  std::memcpy(googl_buy.symbol, "GOOGL", 5);
  googl_buy.side = OrderSide::Buy;
  googl_buy.quantity = 50;

  Fill aapl_sell{};
  std::memset(aapl_sell.symbol, 0, sizeof(aapl_sell.symbol));
  std::memcpy(aapl_sell.symbol, "AAPL", 4);
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
