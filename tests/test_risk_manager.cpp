#include "OrderCooldown.hpp"
#include "PositionManager.hpp"
#include "RiskManager.hpp"
#include "TestHelpers.hpp"
#include "Utils.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <memory>

using test_helpers::createFill;
using test_helpers::createOrder;

class RiskManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    pos_manager_ = std::make_unique<PositionManager>();
    risk_manager_ = std::make_unique<RiskManager>(pos_manager_.get());
  }

  std::unique_ptr<PositionManager> pos_manager_;
  std::unique_ptr<RiskManager> risk_manager_;
};

TEST_F(RiskManagerTest, ApprovesValidOrder) {
  const Order valid_order =
      createOrder("AAPL", OrderSide::Buy, 90, 100 * SCALING_FACTOR);
  EXPECT_TRUE(risk_manager_->onNewOrder(valid_order));
}

TEST_F(RiskManagerTest, RejectsOrderExceedingMaxPosition) {
  const Order existing_order =
      createOrder("AAPL", OrderSide::Buy, 950, 100 * SCALING_FACTOR);
  pos_manager_->onOrderSent(existing_order);

  pos_manager_->onFill(createFill("AAPL", OrderSide::Buy, 950));

  const Order large_order =
      createOrder("AAPL", OrderSide::Buy, 99, 100 * SCALING_FACTOR);
  EXPECT_FALSE(risk_manager_->onNewOrder(large_order));
}

TEST_F(RiskManagerTest, ApprovesOrderWithinMaxPosition) {
  pos_manager_->onFill(createFill("AAPL", OrderSide::Buy, 950));

  const Order okay_order =
      createOrder("AAPL", OrderSide::Buy, 50, 100 * SCALING_FACTOR);
  EXPECT_TRUE(risk_manager_->onNewOrder(okay_order));
}

TEST_F(RiskManagerTest, RejectsOrderExceedingMaxValue) {
  const Order expensive_order =
      createOrder("GOOGL", OrderSide::Buy, 300,
                  200 * static_cast<uint64_t>(SCALING_FACTOR));
  EXPECT_FALSE(risk_manager_->onNewOrder(expensive_order));
}

TEST_F(RiskManagerTest, RejectsOrderWithNullPositionManager) {
  RiskManager risk_manager_no_pos(nullptr);
  const Order order = createOrder("AAPL", OrderSide::Buy, 100, 1000);

  EXPECT_FALSE(risk_manager_no_pos.onNewOrder(order));
}

TEST_F(RiskManagerTest, HandlesZeroPriceOrder) {
  const Order order = createOrder("AAPL", OrderSide::Buy, 100, 0);

  EXPECT_TRUE(risk_manager_->onNewOrder(order));
}

TEST_F(RiskManagerTest, HandlesMaximumAllowedPosition) {
  const Order existing_order =
      createOrder("AAPL", OrderSide::Buy, 999, 100 * SCALING_FACTOR);
  pos_manager_->onOrderSent(existing_order);
  pos_manager_->onFill(createFill("AAPL", OrderSide::Buy, 999));

  Order order = createOrder("AAPL", OrderSide::Buy, 1,
                            100 * static_cast<uint64_t>(SCALING_FACTOR));
  EXPECT_TRUE(risk_manager_->onNewOrder(order));

  Order over_limit_order = createOrder(
      "AAPL", OrderSide::Buy, 2, 100 * static_cast<uint64_t>(SCALING_FACTOR));
  EXPECT_FALSE(risk_manager_->onNewOrder(over_limit_order));
}

TEST_F(RiskManagerTest, HandlesNegativePositionLimits) {
  const Order existing_order =
      createOrder("AAPL", OrderSide::Sell, 999, 100 * SCALING_FACTOR);
  pos_manager_->onOrderSent(existing_order);
  pos_manager_->onFill(createFill("AAPL", OrderSide::Sell, 999));

  const Order order = createOrder("AAPL", OrderSide::Sell, 2,
                                  100 * static_cast<uint64_t>(SCALING_FACTOR));
  EXPECT_FALSE(risk_manager_->onNewOrder(order));
}

TEST_F(RiskManagerTest, HandlesMaxOrderValueBoundary) {
  constexpr uint64_t max_price =
      static_cast<uint64_t>(10000.0 * SCALING_FACTOR / 100);
  const Order max_order = createOrder("AAPL", OrderSide::Buy, 100, max_price);
  EXPECT_TRUE(risk_manager_->onNewOrder(max_order));

  const Order over_limit =
      createOrder("AAPL", OrderSide::Buy, 100, max_price + 1);
  EXPECT_FALSE(risk_manager_->onNewOrder(over_limit));
}

TEST_F(RiskManagerTest, RejectsOrderWhenNotionalCalculationWouldOverflow32Bit) {
  const Order extreme_order =
      createOrder("AAPL", OrderSide::Buy, std::numeric_limits<int64_t>::max(),
                  std::numeric_limits<uint64_t>::max());

  EXPECT_FALSE(risk_manager_->onNewOrder(extreme_order));
}

TEST_F(RiskManagerTest, ConcurrentOnNewOrderFromMultipleThreads) {
  constexpr int num_threads = 4;
  constexpr int orders_per_thread = 250;

  std::atomic<bool> start{false};
  std::atomic<int> approved{0};
  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&]() {
      while (!start.load(std::memory_order_acquire)) {
      }
      for (int i = 0; i < orders_per_thread; ++i) {
        const Order order =
            createOrder("AAPL", OrderSide::Buy, 1, 100 * SCALING_FACTOR);
        if (risk_manager_->onNewOrder(order)) {
          approved.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  start.store(true, std::memory_order_release);

  for (auto &t : threads) {
    t.join();
  }

  const int64_t total_exposure = pos_manager_->getTotalExposure("AAPL");
  EXPECT_EQ(total_exposure, approved.load(std::memory_order_relaxed));
  EXPECT_GE(total_exposure, 1);
}

TEST_F(RiskManagerTest, ApprovesOrderAtLimitWithLargeInputs) {
  constexpr int64_t qty = 10;
  const uint64_t price = static_cast<uint64_t>((10000.0 * SCALING_FACTOR) /
                                               static_cast<long double>(qty));
  const Order order = createOrder("AAPL", OrderSide::Buy, qty, price);

  EXPECT_TRUE(risk_manager_->onNewOrder(order));
}

TEST_F(RiskManagerTest, RejectsOrderDuringCooldown) {
  auto cooldown = std::make_unique<OrderCooldown>(100'000'000);
  cooldown->registerSymbol("AAPL");
  RiskManager rm(pos_manager_.get(), cooldown.get());

  const Order order =
      createOrder("AAPL", OrderSide::Buy, 10, 100 * SCALING_FACTOR);
  EXPECT_TRUE(rm.onNewOrder(order));

  const Order order2 =
      createOrder("AAPL", OrderSide::Buy, 10, 100 * SCALING_FACTOR);
  EXPECT_FALSE(rm.onNewOrder(order2));

  EXPECT_EQ(pos_manager_->getTotalExposure("AAPL"), 10);
}

TEST_F(RiskManagerTest, ApprovesOrderWithNullCooldown) {
  RiskManager rm(pos_manager_.get(), nullptr);

  const Order order =
      createOrder("AAPL", OrderSide::Buy, 10, 100 * SCALING_FACTOR);
  EXPECT_TRUE(rm.onNewOrder(order));

  const Order order2 =
      createOrder("AAPL", OrderSide::Buy, 10, 100 * SCALING_FACTOR);
  EXPECT_TRUE(rm.onNewOrder(order2));
}
