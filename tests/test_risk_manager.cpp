#include "Order.hpp"
#include "PositionManager.hpp"
#include "RiskManager.hpp"
#include <gtest/gtest.h>
#include <memory>

class RiskManagerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        pos_manager_ = std::make_shared<PositionManager>();
        risk_manager_ = std::make_unique<RiskManager>(pos_manager_);
    }

    Order createOrder(const char *symbol, OrderSide side, uint32_t qty,
                      double price) {
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
    Order valid_order = createOrder("AAPL", OrderSide::Buy, 90, 100.00);
    EXPECT_TRUE(risk_manager_->onNewOrder(valid_order));
}

TEST_F(RiskManagerTest, RejectsOrderExceedingMaxPosition) {
    Fill existing_position_fill{};
    strncpy(existing_position_fill.symbol, "AAPL",
            sizeof(existing_position_fill.symbol));
    existing_position_fill.side = OrderSide::Buy;
    existing_position_fill.quantity = 950;
    pos_manager_->onFill(existing_position_fill);

    Order large_order = createOrder("AAPL", OrderSide::Buy, 99, 100.00);
    EXPECT_FALSE(risk_manager_->onNewOrder(large_order));
}

TEST_F(RiskManagerTest, ApprovesOrderWithinMaxPosition) {
    Fill existing_position_fill{};
    strncpy(existing_position_fill.symbol, "AAPL",
            sizeof(existing_position_fill.symbol));
    existing_position_fill.side = OrderSide::Buy;
    existing_position_fill.quantity = 950;
    pos_manager_->onFill(existing_position_fill);

    Order okay_order = createOrder("AAPL", OrderSide::Buy, 50, 100.00);
    EXPECT_TRUE(risk_manager_->onNewOrder(okay_order));
}

TEST_F(RiskManagerTest, RejectsOrderExceedingMaxValue) {
    Order expensive_order = createOrder("GOOGL", OrderSide::Buy, 300, 200.00);
    EXPECT_FALSE(risk_manager_->onNewOrder(expensive_order));
}
