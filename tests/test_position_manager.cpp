#include "PositionManager.hpp"
#include "TestHelpers.hpp"
#include "Utils.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using test_helpers::createFill;
using test_helpers::createOrder;

class PositionManagerTest : public ::testing::Test {
protected:
  PositionManager pm;
};

TEST_F(PositionManagerTest, NewPositionIsZero) {
  EXPECT_EQ(pm.getFilledPosition("AAPL"), 0);
  EXPECT_EQ(pm.getPendingPosition("AAPL"), 0);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 0);
}

struct FillTestParam {
  OrderSide side;
  int64_t quantity;
  int64_t expected_pending;
  int64_t expected_filled;
  int64_t expected_exposure;
};

class PositionManagerFillTest
    : public PositionManagerTest,
      public ::testing::WithParamInterface<FillTestParam> {};

TEST_P(PositionManagerFillTest, ProcessFillUpdatesPosition) {
  const auto &[side, quantity, expected_pending, expected_filled,
               expected_exposure] = GetParam();
  const Order order = createOrder("AAPL", side, quantity, 1000);
  pm.onOrderSent(order);
  pm.onFill(createFill("AAPL", side, quantity));

  EXPECT_EQ(pm.getPendingPosition("AAPL"), expected_pending);
  EXPECT_EQ(pm.getFilledPosition("AAPL"), expected_filled);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), expected_exposure);
}

INSTANTIATE_TEST_SUITE_P(
    BuySell, PositionManagerFillTest,
    ::testing::Values(FillTestParam{OrderSide::Buy, 100, 0, 100, 100},
                      FillTestParam{OrderSide::Sell, 75, 0, -75, -75}));

TEST_F(PositionManagerTest, HandlesMultipleSymbolsAndFills) {
  pm.onFill(createFill("AAPL", OrderSide::Buy, 200));
  pm.onFill(createFill("GOOGL", OrderSide::Buy, 50));
  pm.onFill(createFill("AAPL", OrderSide::Sell, 50));

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

struct OrderSentTestParam {
  OrderSide side;
  int64_t quantity;
  int64_t expected_pending;
  int64_t expected_exposure;
};

class PositionManagerOrderSentTest
    : public PositionManagerTest,
      public ::testing::WithParamInterface<OrderSentTestParam> {};

TEST_P(PositionManagerOrderSentTest, TracksPendingCorrectly) {
  const auto &[side, quantity, expected_pending, expected_exposure] =
      GetParam();
  const Order order = createOrder("AAPL", side, quantity, 1000);
  pm.onOrderSent(order);

  EXPECT_EQ(pm.getFilledPosition("AAPL"), 0);
  EXPECT_EQ(pm.getPendingPosition("AAPL"), expected_pending);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), expected_exposure);
}

INSTANTIATE_TEST_SUITE_P(
    BuySell, PositionManagerOrderSentTest,
    ::testing::Values(OrderSentTestParam{OrderSide::Buy, 100, 100, 100},
                      OrderSentTestParam{OrderSide::Sell, 100, -100, -100}));

TEST_F(PositionManagerTest, PartialFillHandledCorrectly) {
  const Order buy_order = createOrder("AAPL", OrderSide::Buy, 100, 1000);
  pm.onOrderSent(buy_order);
  pm.onFill(createFill("AAPL", OrderSide::Buy, 50));

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
  pm.onFill(createFill("AAPL", OrderSide::Buy, 100));

  const Order order2 = createOrder("AAPL", OrderSide::Buy, 50, 1000);
  pm.onOrderSent(order2);

  EXPECT_EQ(pm.getFilledPosition("AAPL"), 100);
  EXPECT_EQ(pm.getPendingPosition("AAPL"), 50);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 150);
}

TEST_F(PositionManagerTest, ConcurrentFillsForSameSymbol) {
  std::vector<std::thread> threads;
  threads.reserve(4);

  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([this]() {
      const Fill fill = createFill("AAPL", OrderSide::Buy, 1);
      for (int j = 0; j < 250; ++j) {
        pm.onFill(fill);
      }
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  EXPECT_EQ(pm.getFilledPosition("AAPL"), 1000);
}

TEST_F(PositionManagerTest, RegisterSymbolStartsAtZero) {
  pm.registerSymbol("AAPL");

  EXPECT_EQ(pm.getFilledPosition("AAPL"), 0);
  EXPECT_EQ(pm.getPendingPosition("AAPL"), 0);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 0);
}

TEST_F(PositionManagerTest, DoubleRegisterDoesNotResetState) {
  pm.registerSymbol("AAPL");

  const Order filled_order = createOrder("AAPL", OrderSide::Buy, 200, 1000);
  pm.onOrderSent(filled_order);
  pm.onFill(createFill("AAPL", OrderSide::Buy, 200));

  pm.onOrderSent(createOrder("AAPL", OrderSide::Buy, 50, 1000));

  EXPECT_EQ(pm.getFilledPosition("AAPL"), 200);
  EXPECT_EQ(pm.getPendingPosition("AAPL"), 50);

  pm.registerSymbol("AAPL");

  EXPECT_EQ(pm.getFilledPosition("AAPL"), 200);
  EXPECT_EQ(pm.getPendingPosition("AAPL"), 50);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 250);
}

TEST_F(PositionManagerTest, ConcurrentOrderSentAndFill) {
  constexpr int orders_per_thread = 500;
  constexpr int num_sender_threads = 4;

  std::atomic<bool> start{false};
  std::vector<std::thread> threads;
  threads.reserve(num_sender_threads + 1);

  for (int t = 0; t < num_sender_threads; ++t) {
    threads.emplace_back([&]() {
      while (!start.load(std::memory_order_acquire)) {
      }
      for (int i = 0; i < orders_per_thread; ++i) {
        pm.onOrderSent(createOrder("AAPL", OrderSide::Buy, 1, 1000));
      }
    });
  }

  threads.emplace_back([&]() {
    while (!start.load(std::memory_order_acquire)) {
    }
    for (int i = 0; i < num_sender_threads * orders_per_thread; ++i) {
      pm.onFill(createFill("AAPL", OrderSide::Buy, 1));
    }
  });

  start.store(true, std::memory_order_release);

  for (auto &t : threads) {
    t.join();
  }

  const int64_t total_sent =
      static_cast<int64_t>(num_sender_threads) * orders_per_thread;
  EXPECT_EQ(pm.getFilledPosition("AAPL"), total_sent);
  EXPECT_EQ(pm.getPendingPosition("AAPL"), 0);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), total_sent);
}

TEST_F(PositionManagerTest, SequentialOrderFillCancelFlow) {
  const Order order1 = createOrder("AAPL", OrderSide::Buy, 100, 1000);
  pm.onOrderSent(order1);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 100);

  const Order order2 = createOrder("AAPL", OrderSide::Buy, 100, 1000);
  pm.onOrderSent(order2);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 200);

  pm.onFill(createFill("AAPL", OrderSide::Buy, 100));
  EXPECT_EQ(pm.getFilledPosition("AAPL"), 100);
  EXPECT_EQ(pm.getPendingPosition("AAPL"), 100);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 200);

  pm.onOrderCancelled(order2);
  EXPECT_EQ(pm.getFilledPosition("AAPL"), 100);
  EXPECT_EQ(pm.getPendingPosition("AAPL"), 0);
  EXPECT_EQ(pm.getTotalExposure("AAPL"), 100);
}
