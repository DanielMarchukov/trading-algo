#include "PendingOrderTracker.hpp"
#include "ThreadGuard.hpp"
#include <future>
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
  EXPECT_EQ(result->view(), // NOLINT(bugprone-unchecked-optional-access)
            "order-abc");
}

TEST_F(PendingOrderTrackerTest, RecordOverwritesPrevious) {
  tracker_.recordOrder(makeSymbol("AAPL"), OrderSide::Buy, "order-1");
  tracker_.recordOrder(makeSymbol("AAPL"), OrderSide::Buy, "order-2");
  auto result = tracker_.getExistingOrder(makeSymbol("AAPL"), OrderSide::Buy);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->view(), // NOLINT(bugprone-unchecked-optional-access)
            "order-2");
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
  EXPECT_EQ(result->view(), // NOLINT(bugprone-unchecked-optional-access)
            "order-new");
}

struct SlotIndependenceParam {
  const char *desc;
  const char *symbol_a;
  OrderSide side_a;
  const char *id_a;
  const char *symbol_b;
  OrderSide side_b;
  const char *id_b;
};

class SlotIndependenceTest
    : public ::testing::TestWithParam<SlotIndependenceParam> {
protected:
  PendingOrderTracker tracker_;
};

TEST_P(SlotIndependenceTest, SlotsAreIndependent) {
  const auto &[desc, symbol_a, side_a, id_a, symbol_b, side_b, id_b] =
      GetParam();

  tracker_.recordOrder(makeSymbol(symbol_a), side_a, id_a);
  tracker_.recordOrder(makeSymbol(symbol_b), side_b, id_b);

  auto result_a = tracker_.getExistingOrder(makeSymbol(symbol_a), side_a);
  auto result_b = tracker_.getExistingOrder(makeSymbol(symbol_b), side_b);
  ASSERT_TRUE(result_a.has_value());
  ASSERT_TRUE(result_b.has_value());
  EXPECT_EQ(result_a->view(), // NOLINT(bugprone-unchecked-optional-access)
            id_a);
  EXPECT_EQ(result_b->view(), // NOLINT(bugprone-unchecked-optional-access)
            id_b);

  tracker_.onOrderCompleted(makeSymbol(symbol_a), side_a, id_a);
  EXPECT_FALSE(
      tracker_.getExistingOrder(makeSymbol(symbol_a), side_a).has_value());
  EXPECT_TRUE(
      tracker_.getExistingOrder(makeSymbol(symbol_b), side_b).has_value());
}

INSTANTIATE_TEST_SUITE_P(
    SlotTypes, SlotIndependenceTest,
    ::testing::Values(SlotIndependenceParam{"same symbol different sides",
                                            "AAPL", OrderSide::Buy, "buy-order",
                                            "AAPL", OrderSide::Sell,
                                            "sell-order"},
                      SlotIndependenceParam{
                          "different symbols same side", "AAPL", OrderSide::Buy,
                          "aapl-order", "TSLA", OrderSide::Buy, "tsla-order"}));

TEST_F(PendingOrderTrackerTest, ConcurrentAccess) {
  constexpr int kIterations = 1000;
  std::promise<void> done;
  auto future = done.get_future();

  ThreadGuard writer1{std::thread([&]() {
    for (int i = 0; i < kIterations; ++i) {
      tracker_.recordOrder(makeSymbol("AAPL"), OrderSide::Buy,
                           "order-" + std::to_string(i));
    }
  })};

  ThreadGuard writer2{std::thread([&]() {
    for (int i = 0; i < kIterations; ++i) {
      tracker_.recordOrder(makeSymbol("AAPL"), OrderSide::Sell,
                           "order-" + std::to_string(i));
    }
  })};

  ThreadGuard reader{std::thread([&]() {
    for (int i = 0; i < kIterations; ++i) {
      (void)tracker_.getExistingOrder(makeSymbol("AAPL"), OrderSide::Buy);
      (void)tracker_.getExistingOrder(makeSymbol("AAPL"), OrderSide::Sell);
    }
    done.set_value();
  })};

  const auto status = future.wait_for(std::chrono::seconds(5));
  ASSERT_EQ(status, std::future_status::ready)
      << "ConcurrentAccess test timed out — possible deadlock";

  writer1.t.join();
  writer2.t.join();
  reader.t.join();
}
