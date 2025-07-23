#include "Order.hpp"
#include "PositionManager.hpp"
#include "RiskManager.hpp"
#include "Utils.hpp"
#include <gtest/gtest.h>
#include <memory>

class RiskManagerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        pos_manager_ = std::make_shared<PositionManager>();
        risk_manager_ = std::make_unique<RiskManager>(pos_manager_);
    }

    static Order createOrder(const char *symbol, const OrderSide side,
                             const int32_t qty, const uint32_t price) {
        Order order{};
        strncpy(order.symbol, symbol, sizeof(order.symbol));
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
    strncpy(existing_position_fill.symbol, "AAPL",
            sizeof(existing_position_fill.symbol));
    existing_position_fill.side = OrderSide::Buy;
    existing_position_fill.quantity = 950;
    pos_manager_->onFill(existing_position_fill);

    const Order large_order =
        createOrder("AAPL", OrderSide::Buy, 99, 100 * SCALING_FACTOR);
    EXPECT_FALSE(risk_manager_->onNewOrder(large_order));
}

TEST_F(RiskManagerTest, ApprovesOrderWithinMaxPosition) {
    Fill existing_position_fill{};
    strncpy(existing_position_fill.symbol, "AAPL",
            sizeof(existing_position_fill.symbol));
    existing_position_fill.side = OrderSide::Buy;
    existing_position_fill.quantity = 950;
    pos_manager_->onFill(existing_position_fill);

    const Order okay_order =
        createOrder("AAPL", OrderSide::Buy, 50, 100 * SCALING_FACTOR);
    EXPECT_TRUE(risk_manager_->onNewOrder(okay_order));
}

TEST_F(RiskManagerTest, RejectsOrderExceedingMaxValue) {
    const Order expensive_order =
        createOrder("GOOGL", OrderSide::Buy, 300, 200 * SCALING_FACTOR);
    EXPECT_FALSE(risk_manager_->onNewOrder(expensive_order));
}

TEST_F(RiskManagerTest, RejectsOrderWithNullPositionManager) {
    const RiskManager risk_manager_no_pos(nullptr);
    const Order order = createOrder("AAPL", OrderSide::Buy, 100, 1000);

    EXPECT_FALSE(risk_manager_no_pos.onNewOrder(order));
}

TEST_F(RiskManagerTest, HandlesZeroPriceOrder) {
    const Order order = createOrder("AAPL", OrderSide::Buy, 100, 0);

    EXPECT_TRUE(risk_manager_->onNewOrder(order));
}

TEST_F(RiskManagerTest, HandlesMaximumAllowedPosition) {
    Fill existing_fill{};
    strncpy(existing_fill.symbol, "AAPL", sizeof(existing_fill.symbol));
    existing_fill.side = OrderSide::Buy;
    existing_fill.quantity = 999;
    pos_manager_->onFill(existing_fill);

    // Order that would reach exactly the limit should be approved
    Order order = createOrder("AAPL", OrderSide::Buy, 1, 100 * SCALING_FACTOR);
    EXPECT_TRUE(risk_manager_->onNewOrder(order));

    // Order that would exceed the limit should be rejected
    Order over_limit_order =
        createOrder("AAPL", OrderSide::Buy, 2, 100 * SCALING_FACTOR);
    EXPECT_FALSE(risk_manager_->onNewOrder(over_limit_order));
}

TEST_F(RiskManagerTest, HandlesNegativePositionLimits) {
    Fill short_fill{};
    strncpy(short_fill.symbol, "AAPL", sizeof(short_fill.symbol));
    short_fill.side = OrderSide::Sell;
    short_fill.quantity = 999;
    pos_manager_->onFill(short_fill);

    const Order order =
        createOrder("AAPL", OrderSide::Sell, 2, 100 * SCALING_FACTOR);
    EXPECT_FALSE(risk_manager_->onNewOrder(order));
}

TEST_F(RiskManagerTest, HandlesMaxOrderValueBoundary) {
    constexpr uint32_t max_price = (10000 * SCALING_FACTOR) / 100;
    const Order max_order = createOrder("AAPL", OrderSide::Buy, 100, max_price);
    EXPECT_TRUE(risk_manager_->onNewOrder(max_order));

    const Order over_limit =
        createOrder("AAPL", OrderSide::Buy, 100, max_price + 1);
    EXPECT_FALSE(risk_manager_->onNewOrder(over_limit));
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
