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

  static Fill createFill(const char *symbol, const OrderSide side,
                         const int64_t qty) {
    Fill fill{};
    std::memset(fill.symbol, 0, sizeof(fill.symbol));
    const std::size_t copy_len =
        (std::min)(std::strlen(symbol), sizeof(fill.symbol));
    std::memcpy(fill.symbol, symbol, copy_len);
    fill.side = side;
    fill.quantity = qty;
    return fill;
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

TEST_F(RiskManagerTest, ApprovesOrderAtLimitWithLargeInputs) {
  constexpr int64_t qty = 10;
  const uint64_t price = static_cast<uint64_t>((10000.0 * SCALING_FACTOR) /
                                               static_cast<long double>(qty));
  const Order order = createOrder("AAPL", OrderSide::Buy, qty, price);

  EXPECT_TRUE(risk_manager_->onNewOrder(order));
}
