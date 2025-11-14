#include "Fill.hpp"
#include "PositionManager.hpp"
#include "Utils.hpp"
#include <cstring>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

class PositionManagerTest : public ::testing::Test {
protected:
  PositionManager pm;

  static Order createOrder(const char *symbol, const OrderSide side,
                           const int64_t qty, const uint64_t price) {
    Order order{};
    std::memset(order.symbol, 0, sizeof(order.symbol));
    const std::size_t copy_len =
        std::min(std::strlen(symbol), sizeof(order.symbol));
    std::memcpy(order.symbol, symbol, copy_len);
    order.side = side;
    order.quantity = qty;
    order.price = price;
    return order;
  }
};

TEST_F(PositionManagerTest, NewPositionIsZero) {
  EXPECT_EQ(pm.getFilledPosition("AAPL"), 0);
  EXPECT_EQ(pm.getPendingPosition("AAPL"), 0);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 0);
}

TEST_F(PositionManagerTest, ProcessBuyFill) {
  Fill buy_fill{};
  std::memset(buy_fill.symbol, 0, sizeof(buy_fill.symbol));
  std::memcpy(buy_fill.symbol, "AAPL", 4);
  buy_fill.side = OrderSide::Buy;
  buy_fill.quantity = 100;

  pm.onFill(buy_fill);

  EXPECT_EQ(pm.getFilledPosition("AAPL"), 100);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 100);
}

TEST_F(PositionManagerTest, ProcessSellFill) {
  Fill sell_fill{};
  std::memset(sell_fill.symbol, 0, sizeof(sell_fill.symbol));
  std::memcpy(sell_fill.symbol, "AAPL", 4);
  sell_fill.side = OrderSide::Sell;
  sell_fill.quantity = 75;

  pm.onFill(sell_fill);

  EXPECT_EQ(pm.getFilledPosition("AAPL"), -75);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), -75);
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

  EXPECT_EQ(pm.getFilledPosition("AAPL"), 150);
  EXPECT_EQ(pm.getFilledPosition("GOOGL"), 50);
}

TEST_F(PositionManagerTest, HandlesMaxLengthSymbol) {
  Fill fill{};
  memset(fill.symbol, 'X', sizeof(fill.symbol) - 1);
  fill.symbol[sizeof(fill.symbol) - 1] = '\0';
  fill.side = OrderSide::Buy;
  fill.quantity = 100;

  pm.onFill(fill);

  EXPECT_EQ(pm.getFilledPosition(fill.symbol), 100);
}

TEST_F(PositionManagerTest, HandlesEmptySymbol) {
  Fill fill{};
  fill.symbol[0] = '\0';
  fill.side = OrderSide::Buy;
  fill.quantity = 50;

  pm.onFill(fill);

  EXPECT_EQ(pm.getFilledPosition(""), 50);
}

TEST_F(PositionManagerTest, OnOrderSentIncrementsPending) {
  const Order buy_order = createOrder("AAPL", OrderSide::Buy, 100, 1000);
  pm.onOrderSent(buy_order);

  EXPECT_EQ(pm.getFilledPosition("AAPL"), 0);
  EXPECT_EQ(pm.getPendingPosition("AAPL"), 100);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 100);
}

TEST_F(PositionManagerTest, OnOrderSentSellDecrementsPending) {
  const Order sell_order = createOrder("AAPL", OrderSide::Sell, 100, 1000);
  pm.onOrderSent(sell_order);

  EXPECT_EQ(pm.getFilledPosition("AAPL"), 0);
  EXPECT_EQ(pm.getPendingPosition("AAPL"), -100);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), -100);
}

TEST_F(PositionManagerTest, OnFillMovesFromPendingToFilled) {
  const Order buy_order = createOrder("AAPL", OrderSide::Buy, 100, 1000);
  pm.onOrderSent(buy_order);

  Fill fill{};
  std::memset(fill.symbol, 0, sizeof(fill.symbol));
  std::memcpy(fill.symbol, "AAPL", 4);
  fill.side = OrderSide::Buy;
  fill.quantity = 100;
  pm.onFill(fill);

  EXPECT_EQ(pm.getPendingPosition("AAPL"), 0);
  EXPECT_EQ(pm.getFilledPosition("AAPL"), 100);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 100);
}

TEST_F(PositionManagerTest, PartialFillHandledCorrectly) {
  const Order buy_order = createOrder("AAPL", OrderSide::Buy, 100, 1000);
  pm.onOrderSent(buy_order);

  Fill partial_fill{};
  std::memset(partial_fill.symbol, 0, sizeof(partial_fill.symbol));
  std::memcpy(partial_fill.symbol, "AAPL", 4);
  partial_fill.side = OrderSide::Buy;
  partial_fill.quantity = 50;
  pm.onFill(partial_fill);

  EXPECT_EQ(pm.getPendingPosition("AAPL"), 50);
  EXPECT_EQ(pm.getFilledPosition("AAPL"), 50);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 100);
}

TEST_F(PositionManagerTest, OnOrderCancelledReleasesPending) {
  const Order buy_order = createOrder("AAPL", OrderSide::Buy, 100, 1000);
  pm.onOrderSent(buy_order);
  pm.onOrderCancelled(buy_order);

  EXPECT_EQ(pm.getPendingPosition("AAPL"), 0);
  EXPECT_EQ(pm.getFilledPosition("AAPL"), 0);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 0);
}

TEST_F(PositionManagerTest, MultipleOrdersAccumulatePending) {
  for (int i = 0; i < 5; ++i) {
    const Order buy_order = createOrder("AAPL", OrderSide::Buy, 100, 1000);
    pm.onOrderSent(buy_order);
  }

  EXPECT_EQ(pm.getPendingPosition("AAPL"), 500);
  EXPECT_EQ(pm.getFilledPosition("AAPL"), 0);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 500);
}

TEST_F(PositionManagerTest, MixedBuySellOrdersNetOut) {
  const Order buy_order = createOrder("AAPL", OrderSide::Buy, 100, 1000);
  pm.onOrderSent(buy_order);

  const Order sell_order = createOrder("AAPL", OrderSide::Sell, 50, 1000);
  pm.onOrderSent(sell_order);

  EXPECT_EQ(pm.getPendingPosition("AAPL"), 50);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 50);
}

TEST_F(PositionManagerTest, FilledAndPendingIndependent) {
  const Order order1 = createOrder("AAPL", OrderSide::Buy, 100, 1000);
  pm.onOrderSent(order1);

  Fill fill1{};
  std::memset(fill1.symbol, 0, sizeof(fill1.symbol));
  std::memcpy(fill1.symbol, "AAPL", 4);
  fill1.side = OrderSide::Buy;
  fill1.quantity = 100;
  pm.onFill(fill1);

  const Order order2 = createOrder("AAPL", OrderSide::Buy, 50, 1000);
  pm.onOrderSent(order2);

  EXPECT_EQ(pm.getFilledPosition("AAPL"), 100);
  EXPECT_EQ(pm.getPendingPosition("AAPL"), 50);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 150);
}

TEST_F(PositionManagerTest, ConcurrentFillsForSameSymbol) {
  std::vector<std::thread> threads;

  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([this]() {
      for (int j = 0; j < 250; ++j) {
        Fill fill{};
        std::memset(fill.symbol, 0, sizeof(fill.symbol));
        std::memcpy(fill.symbol, "AAPL", 4);
        fill.side = OrderSide::Buy;
        fill.quantity = 1;
        pm.onFill(fill);
      }
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  EXPECT_EQ(pm.getFilledPosition("AAPL"), 1000);
}

TEST_F(PositionManagerTest, SequentialOrderFillCancelFlow) {
  const Order order1 = createOrder("AAPL", OrderSide::Buy, 100, 1000);
  pm.onOrderSent(order1);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 100);

  const Order order2 = createOrder("AAPL", OrderSide::Buy, 100, 1000);
  pm.onOrderSent(order2);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 200);

  Fill fill1{};
  std::memset(fill1.symbol, 0, sizeof(fill1.symbol));
  std::memcpy(fill1.symbol, "AAPL", 4);
  fill1.side = OrderSide::Buy;
  fill1.quantity = 100;
  pm.onFill(fill1);
  EXPECT_EQ(pm.getFilledPosition("AAPL"), 100);
  EXPECT_EQ(pm.getPendingPosition("AAPL"), 100);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 200);

  pm.onOrderCancelled(order2);
  EXPECT_EQ(pm.getFilledPosition("AAPL"), 100);
  EXPECT_EQ(pm.getPendingPosition("AAPL"), 0);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 100);
}
