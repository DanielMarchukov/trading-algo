#include "Order.hpp"
#include "PositionManager.hpp"
#include "RiskManager.hpp"
#include "Utils.hpp"
#include <algorithm>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>
#include <memory>

class RiskManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    pos_manager_ = std::make_shared<PositionManager>();
    risk_manager_ = std::make_unique<RiskManager>(pos_manager_);
  }

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

  std::shared_ptr<PositionManager> pos_manager_;
  std::unique_ptr<RiskManager> risk_manager_;
};

TEST_F(RiskManagerTest, ApprovesValidOrder) {
  const Order valid_order =
      createOrder("AAPL", OrderSide::Buy, 90, 100 * SCALING_FACTOR);
  EXPECT_TRUE(risk_manager_->onNewOrder(valid_order));
}

TEST_F(RiskManagerTest, RejectsOrderExceedingMaxPosition) {
  Fill existing_position_fill{};
  std::memset(existing_position_fill.symbol, 0,
              sizeof(existing_position_fill.symbol));
  std::memcpy(existing_position_fill.symbol, "AAPL", 4);
  existing_position_fill.side = OrderSide::Buy;
  existing_position_fill.quantity = 950;
  pos_manager_->onFill(existing_position_fill);

  const Order large_order =
      createOrder("AAPL", OrderSide::Buy, 99, 100 * SCALING_FACTOR);
  EXPECT_FALSE(risk_manager_->onNewOrder(large_order));
}

TEST_F(RiskManagerTest, ApprovesOrderWithinMaxPosition) {
  Fill existing_position_fill{};
  std::memset(existing_position_fill.symbol, 0,
              sizeof(existing_position_fill.symbol));
  std::memcpy(existing_position_fill.symbol, "AAPL", 4);
  existing_position_fill.side = OrderSide::Buy;
  existing_position_fill.quantity = 950;
  pos_manager_->onFill(existing_position_fill);

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
  Fill existing_fill{};
  std::memset(existing_fill.symbol, 0, sizeof(existing_fill.symbol));
  std::memcpy(existing_fill.symbol, "AAPL", 4);
  existing_fill.side = OrderSide::Buy;
  existing_fill.quantity = 999;
  pos_manager_->onFill(existing_fill);

  // Order that would reach exactly the limit should be approved
  Order order = createOrder("AAPL", OrderSide::Buy, 1,
                            100 * static_cast<uint64_t>(SCALING_FACTOR));
  EXPECT_TRUE(risk_manager_->onNewOrder(order));

  // Order that would exceed the limit should be rejected
  Order over_limit_order = createOrder(
      "AAPL", OrderSide::Buy, 2, 100 * static_cast<uint64_t>(SCALING_FACTOR));
  EXPECT_FALSE(risk_manager_->onNewOrder(over_limit_order));
}

TEST_F(RiskManagerTest, HandlesNegativePositionLimits) {
  Fill short_fill{};
  std::memset(short_fill.symbol, 0, sizeof(short_fill.symbol));
  std::memcpy(short_fill.symbol, "AAPL", 4);
  short_fill.side = OrderSide::Sell;
  short_fill.quantity = 999;
  pos_manager_->onFill(short_fill);

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

TEST_F(RiskManagerTest, ApprovesOrderAtLimitWithLargeInputs) {
  constexpr int64_t qty = 10;
  const uint64_t price = static_cast<uint64_t>((10000.0 * SCALING_FACTOR) /
                                               static_cast<long double>(qty));
  const Order order = createOrder("AAPL", OrderSide::Buy, qty, price);

  EXPECT_TRUE(risk_manager_->onNewOrder(order));
}

TEST_F(RiskManagerTest, HandlesDifferentOrderTypes) {
  Order market_order =
      createOrder("AAPL", OrderSide::Buy, 10, 100 * SCALING_FACTOR);
  market_order.type = OrderType::Market;
  EXPECT_TRUE(risk_manager_->onNewOrder(market_order));

  Order limit_order =
      createOrder("AAPL", OrderSide::Buy, 10, 100 * SCALING_FACTOR);
  limit_order.type = OrderType::Limit;
  EXPECT_TRUE(risk_manager_->onNewOrder(limit_order));
}
