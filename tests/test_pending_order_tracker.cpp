#include "PendingOrderTracker.hpp"
#include <gtest/gtest.h>
#include <thread>

namespace {

SymbolKey makeSymbol(const char *s) {
  SymbolKey key{};
  std::memset(key.value, 0, sizeof(key.value));
  auto len = (std::min)(std::strlen(s), sizeof(key.value));
  std::memcpy(key.value, s, len);
  return key;
}

} // namespace

class PendingOrderTrackerTest : public ::testing::Test {
protected:
  PendingOrderTracker tracker_;
};

TEST_F(PendingOrderTrackerTest, GetReturnsNulloptWhenEmpty) {
  EXPECT_FALSE(tracker_.getExistingOrder(makeSymbol("AAPL"), OrderSide::Buy)
                   .has_value());
}

TEST_F(PendingOrderTrackerTest, RecordAndRetrieve) {
  tracker_.recordOrder(makeSymbol("AAPL"), OrderSide::Buy, "order-abc");
  auto result = tracker_.getExistingOrder(makeSymbol("AAPL"), OrderSide::Buy);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, "order-abc");
}

TEST_F(PendingOrderTrackerTest, RecordOverwritesPrevious) {
  tracker_.recordOrder(makeSymbol("AAPL"), OrderSide::Buy, "order-1");
  tracker_.recordOrder(makeSymbol("AAPL"), OrderSide::Buy, "order-2");
  auto result = tracker_.getExistingOrder(makeSymbol("AAPL"), OrderSide::Buy);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, "order-2");
}

TEST_F(PendingOrderTrackerTest, OnCompletedRemovesMatchingId) {
  tracker_.recordOrder(makeSymbol("AAPL"), OrderSide::Buy, "order-abc");
  tracker_.onOrderCompleted(makeSymbol("AAPL"), OrderSide::Buy, "order-abc");
  EXPECT_FALSE(tracker_.getExistingOrder(makeSymbol("AAPL"), OrderSide::Buy)
                   .has_value());
}

TEST_F(PendingOrderTrackerTest, OnCompletedIgnoresMismatchedId) {
  tracker_.recordOrder(makeSymbol("AAPL"), OrderSide::Buy, "order-new");
  tracker_.onOrderCompleted(makeSymbol("AAPL"), OrderSide::Buy, "order-old");
  auto result = tracker_.getExistingOrder(makeSymbol("AAPL"), OrderSide::Buy);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, "order-new");
}

TEST_F(PendingOrderTrackerTest, IndependentSlots) {
  tracker_.recordOrder(makeSymbol("AAPL"), OrderSide::Buy, "buy-order");
  tracker_.recordOrder(makeSymbol("AAPL"), OrderSide::Sell, "sell-order");

  auto buy = tracker_.getExistingOrder(makeSymbol("AAPL"), OrderSide::Buy);
  auto sell = tracker_.getExistingOrder(makeSymbol("AAPL"), OrderSide::Sell);
  ASSERT_TRUE(buy.has_value());
  ASSERT_TRUE(sell.has_value());
  EXPECT_EQ(*buy, "buy-order");
  EXPECT_EQ(*sell, "sell-order");

  tracker_.onOrderCompleted(makeSymbol("AAPL"), OrderSide::Buy, "buy-order");
  EXPECT_FALSE(tracker_.getExistingOrder(makeSymbol("AAPL"), OrderSide::Buy)
                   .has_value());
  EXPECT_TRUE(tracker_.getExistingOrder(makeSymbol("AAPL"), OrderSide::Sell)
                  .has_value());
}

TEST_F(PendingOrderTrackerTest, DifferentSymbolsAreIndependent) {
  tracker_.recordOrder(makeSymbol("AAPL"), OrderSide::Buy, "aapl-order");
  tracker_.recordOrder(makeSymbol("TSLA"), OrderSide::Buy, "tsla-order");

  auto aapl = tracker_.getExistingOrder(makeSymbol("AAPL"), OrderSide::Buy);
  auto tsla = tracker_.getExistingOrder(makeSymbol("TSLA"), OrderSide::Buy);
  ASSERT_TRUE(aapl.has_value());
  ASSERT_TRUE(tsla.has_value());
  EXPECT_EQ(*aapl, "aapl-order");
  EXPECT_EQ(*tsla, "tsla-order");
}

TEST_F(PendingOrderTrackerTest, ConcurrentAccess) {
  constexpr int kIterations = 1000;

  std::thread writer1([&]() {
    for (int i = 0; i < kIterations; ++i) {
      tracker_.recordOrder(makeSymbol("AAPL"), OrderSide::Buy,
                           "order-" + std::to_string(i));
    }
  });

  std::thread writer2([&]() {
    for (int i = 0; i < kIterations; ++i) {
      tracker_.recordOrder(makeSymbol("AAPL"), OrderSide::Sell,
                           "order-" + std::to_string(i));
    }
  });

  std::thread reader([&]() {
    for (int i = 0; i < kIterations; ++i) {
      (void)tracker_.getExistingOrder(makeSymbol("AAPL"), OrderSide::Buy);
      (void)tracker_.getExistingOrder(makeSymbol("AAPL"), OrderSide::Sell);
    }
  });

  writer1.join();
  writer2.join();
  reader.join();
}
