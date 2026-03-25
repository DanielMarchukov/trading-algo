#include "PositionManager.hpp"
#include "TestHelpers.hpp"
#include "ThreadGuard.hpp"
#include <cmath>
#include <future>
#include <gtest/gtest.h>
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
  constexpr int kThreads = 4;
  std::atomic<int> done_count{0};
  std::promise<void> all_done;

  std::vector<ThreadGuard> threads;
  threads.reserve(kThreads);

  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back(std::thread([this, &done_count, &all_done]() {
      const Fill fill = createFill("AAPL", OrderSide::Buy, 1);
      for (int j = 0; j < 250; ++j) {
        pm.onFill(fill);
      }
      if (done_count.fetch_add(1, std::memory_order_acq_rel) + 1 == kThreads) {
        all_done.set_value();
      }
    }));
  }

  auto status = all_done.get_future().wait_for(std::chrono::seconds(5));
  ASSERT_EQ(status, std::future_status::ready) << "timeout — possible deadlock";
  threads.clear();

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
  constexpr int total_threads = num_sender_threads + 1;

  std::atomic<bool> start{false};
  std::atomic<int> done_count{0};
  std::promise<void> all_done;

  std::vector<ThreadGuard> threads;
  threads.reserve(total_threads);

  for (int t = 0; t < num_sender_threads; ++t) {
    threads.emplace_back(std::thread([&]() {
      while (!start.load(std::memory_order_acquire)) {
      }
      for (int i = 0; i < orders_per_thread; ++i) {
        pm.onOrderSent(createOrder("AAPL", OrderSide::Buy, 1, 1000));
      }
      if (done_count.fetch_add(1, std::memory_order_acq_rel) + 1 ==
          total_threads) {
        all_done.set_value();
      }
    }));
  }

  threads.emplace_back(std::thread([&]() {
    while (!start.load(std::memory_order_acquire)) {
    }
    for (int i = 0; i < num_sender_threads * orders_per_thread; ++i) {
      pm.onFill(createFill("AAPL", OrderSide::Buy, 1));
    }
    if (done_count.fetch_add(1, std::memory_order_acq_rel) + 1 ==
        total_threads) {
      all_done.set_value();
    }
  }));

  start.store(true, std::memory_order_release);

  auto status = all_done.get_future().wait_for(std::chrono::seconds(5));
  ASSERT_EQ(status, std::future_status::ready) << "timeout — possible deadlock";
  threads.clear();

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

TEST_F(PositionManagerTest, NewPositionHasZeroPnl) {
  EXPECT_EQ(pm.getCostBasis("AAPL"), 0);
  EXPECT_EQ(pm.getRealizedPnl("AAPL"), 0);
}

TEST_F(PositionManagerTest, MultipleBuysWeightedAverage) {
  pm.onFill(createFill("AAPL", OrderSide::Buy, 50, 100.0));
  pm.onFill(createFill("AAPL", OrderSide::Buy, 50, 200.0));
  pm.onFill(createFill("AAPL", OrderSide::Sell, 100, 175.0));

  EXPECT_EQ(pm.getRealizedPnl("AAPL"), 25LL * SCALING_FACTOR * 100);
  EXPECT_EQ(pm.getCostBasis("AAPL"), 0);
}

TEST_F(PositionManagerTest, PnlAccumulatesAcrossMultipleTrades) {
  pm.onFill(createFill("AAPL", OrderSide::Buy, 100, 100.0));
  pm.onFill(createFill("AAPL", OrderSide::Sell, 100, 110.0));
  pm.onFill(createFill("AAPL", OrderSide::Buy, 100, 120.0));
  pm.onFill(createFill("AAPL", OrderSide::Sell, 100, 125.0));

  const int64_t expected_pnl =
      (10LL * SCALING_FACTOR * 100) + (5LL * SCALING_FACTOR * 100);
  EXPECT_EQ(pm.getRealizedPnl("AAPL"), expected_pnl);
  EXPECT_EQ(pm.getCostBasis("AAPL"), 0);
}

struct PnLTestParam {
  const char *name;
  OrderSide side;
  int64_t quantity;
  double price;
  int64_t expected_filled;
  int64_t expected_cost_basis;
  int64_t expected_realized_pnl;
};

class PositionManagerPnLTest
    : public PositionManagerTest,
      public ::testing::WithParamInterface<PnLTestParam> {};

TEST_P(PositionManagerPnLTest, SingleFillPnl) {
  const auto &p = GetParam();
  pm.onFill(createFill("AAPL", p.side, p.quantity, p.price));

  EXPECT_EQ(pm.getFilledPosition("AAPL"), p.expected_filled);
  EXPECT_EQ(pm.getCostBasis("AAPL"), p.expected_cost_basis);
  EXPECT_EQ(pm.getRealizedPnl("AAPL"), p.expected_realized_pnl);
}

INSTANTIATE_TEST_SUITE_P(
    PnLCases, PositionManagerPnLTest,
    ::testing::Values(PnLTestParam{"BuyOpensLong", OrderSide::Buy, 100, 150.0,
                                   100, 150LL * SCALING_FACTOR * 100, 0},
                      PnLTestParam{"SellOpensShort", OrderSide::Sell, 100,
                                   150.0, -100, -150LL * SCALING_FACTOR * 100,
                                   0},
                      PnLTestParam{"SmallBuy", OrderSide::Buy, 1, 99.99, 1,
                                   std::llround(99.99 * SCALING_FACTOR) * 1,
                                   0}),
    [](const ::testing::TestParamInfo<PnLTestParam> &info) {
      return info.param.name;
    });

struct TwoFillPnLParam {
  const char *name;
  OrderSide side1;
  int64_t qty1;
  double price1;
  OrderSide side2;
  int64_t qty2;
  double price2;
  int64_t expected_filled;
  int64_t expected_cost_basis;
  int64_t expected_realized_pnl;
};

class PositionManagerTwoFillPnLTest
    : public PositionManagerTest,
      public ::testing::WithParamInterface<TwoFillPnLParam> {};

TEST_P(PositionManagerTwoFillPnLTest, TwoFillsProducePnl) {
  const auto &p = GetParam();
  pm.onFill(createFill("AAPL", p.side1, p.qty1, p.price1));
  pm.onFill(createFill("AAPL", p.side2, p.qty2, p.price2));

  EXPECT_EQ(pm.getFilledPosition("AAPL"), p.expected_filled);
  EXPECT_EQ(pm.getCostBasis("AAPL"), p.expected_cost_basis);
  EXPECT_EQ(pm.getRealizedPnl("AAPL"), p.expected_realized_pnl);
}

INSTANTIATE_TEST_SUITE_P(
    PnLTwoFill, PositionManagerTwoFillPnLTest,
    ::testing::Values(TwoFillPnLParam{"FullSellRealizesPnl", OrderSide::Buy,
                                      100, 150.0, OrderSide::Sell, 100, 155.0,
                                      0, 0, 5LL * SCALING_FACTOR * 100},
                      TwoFillPnLParam{"PartialSellProRates", OrderSide::Buy,
                                      100, 150.0, OrderSide::Sell, 50, 160.0,
                                      50, 150LL * SCALING_FACTOR * 50,
                                      10LL * SCALING_FACTOR * 50},
                      TwoFillPnLParam{"BuyToCoverShort", OrderSide::Sell, 100,
                                      150.0, OrderSide::Buy, 100, 140.0, 0, 0,
                                      10LL * SCALING_FACTOR * 100},
                      TwoFillPnLParam{"RoundTripExactZero", OrderSide::Buy, 100,
                                      150.25, OrderSide::Sell, 100, 150.25, 0,
                                      0, 0},
                      TwoFillPnLParam{"SellReversesLongToShort", OrderSide::Buy,
                                      50, 100.0, OrderSide::Sell, 80, 110.0,
                                      -30, -110LL * SCALING_FACTOR * 30,
                                      10LL * SCALING_FACTOR * 50},
                      TwoFillPnLParam{"BuyReversesShortToLong", OrderSide::Sell,
                                      50, 100.0, OrderSide::Buy, 80, 90.0, 30,
                                      90LL * SCALING_FACTOR * 30,
                                      10LL * SCALING_FACTOR * 50}),
    [](const ::testing::TestParamInfo<TwoFillPnLParam> &info) {
      return info.param.name;
    });

TEST_F(PositionManagerTest, ConcurrentFillsPreservePnlConsistency) {
  constexpr int kThreads = 4;
  constexpr int kFillsPerThread = 250;
  constexpr double kPrice = 100.0;
  std::atomic<int> done_count{0};
  std::promise<void> all_done;

  std::vector<ThreadGuard> threads;
  threads.reserve(kThreads);

  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back(std::thread([this, &done_count, &all_done]() {
      for (int j = 0; j < kFillsPerThread; ++j) {
        pm.onFill(createFill("AAPL", OrderSide::Buy, 1, kPrice));
      }
      if (done_count.fetch_add(1, std::memory_order_acq_rel) + 1 == kThreads) {
        all_done.set_value();
      }
    }));
  }

  auto status = all_done.get_future().wait_for(std::chrono::seconds(5));
  ASSERT_EQ(status, std::future_status::ready) << "timeout";
  threads.clear();

  const int64_t total_qty = static_cast<int64_t>(kThreads) * kFillsPerThread;
  EXPECT_EQ(pm.getFilledPosition("AAPL"), total_qty);
  EXPECT_EQ(pm.getCostBasis("AAPL"),
            static_cast<int64_t>(kPrice * SCALING_FACTOR) * total_qty);
  EXPECT_EQ(pm.getRealizedPnl("AAPL"), 0);
}
