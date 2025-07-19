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

    static Order createOrder(const char *symbol, const OrderSide side, const uint32_t qty,
                      const uint32_t price) {
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
    const Order valid_order = createOrder("AAPL", OrderSide::Buy, 90, 100 * SCALING_FACTOR);
    EXPECT_TRUE(risk_manager_->onNewOrder(valid_order));
}

TEST_F(RiskManagerTest, RejectsOrderExceedingMaxPosition) {
    Fill existing_position_fill{};
    strncpy(existing_position_fill.symbol, "AAPL",
            sizeof(existing_position_fill.symbol));
    existing_position_fill.side = OrderSide::Buy;
    existing_position_fill.quantity = 950;
    pos_manager_->onFill(existing_position_fill);

    const Order large_order = createOrder("AAPL", OrderSide::Buy, 99, 100 * SCALING_FACTOR);
    EXPECT_FALSE(risk_manager_->onNewOrder(large_order));
}

TEST_F(RiskManagerTest, ApprovesOrderWithinMaxPosition) {
    Fill existing_position_fill{};
    strncpy(existing_position_fill.symbol, "AAPL",
            sizeof(existing_position_fill.symbol));
    existing_position_fill.side = OrderSide::Buy;
    existing_position_fill.quantity = 950;
    pos_manager_->onFill(existing_position_fill);

    const Order okay_order = createOrder("AAPL", OrderSide::Buy, 50, 100 * SCALING_FACTOR);
    EXPECT_TRUE(risk_manager_->onNewOrder(okay_order));
}

TEST_F(RiskManagerTest, RejectsOrderExceedingMaxValue) {
    const Order expensive_order = createOrder("GOOGL", OrderSide::Buy, 300, 200 * SCALING_FACTOR);
    EXPECT_FALSE(risk_manager_->onNewOrder(expensive_order));
}
