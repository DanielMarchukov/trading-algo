#include "AlpacaFillListener.hpp"
#include "PositionManager.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <string>

class FillListenerTest : public ::testing::Test {
protected:
  void SetUp() override {
    position_manager_ = std::make_shared<PositionManager>();
    position_manager_->registerSymbol("AAPL");
    position_manager_->registerSymbol("GOOGL");
  }

  std::shared_ptr<PositionManager> position_manager_;
};

TEST_F(FillListenerTest, ParsesFillEvent) {
  const std::string json = R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "100",
      "price": "150.25",
      "order": {
        "symbol": "AAPL",
        "side": "buy"
      }
    }
  })";

  const TradeUpdate update = parseTradingUpdate(json);
  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));

  const auto &fill = std::get<FillEvent>(update);
  EXPECT_EQ(std::string_view(fill.symbol, 4), "AAPL");
  EXPECT_EQ(fill.side, OrderSide::Buy);
  EXPECT_EQ(fill.quantity, 100);
  EXPECT_DOUBLE_EQ(fill.price, 150.25);
}

TEST_F(FillListenerTest, ParsesPartialFillEvent) {
  const std::string json = R"({
    "stream": "trade_updates",
    "data": {
      "event": "partial_fill",
      "qty": "50",
      "price": "200.00",
      "order": {
        "symbol": "GOOGL",
        "side": "sell"
      }
    }
  })";

  const TradeUpdate update = parseTradingUpdate(json);
  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));

  const auto &fill = std::get<FillEvent>(update);
  EXPECT_EQ(fill.side, OrderSide::Sell);
  EXPECT_EQ(fill.quantity, 50);
  EXPECT_DOUBLE_EQ(fill.price, 200.0);
}

TEST_F(FillListenerTest, ParsesCanceledEvent) {
  const std::string json = R"({
    "stream": "trade_updates",
    "data": {
      "event": "canceled",
      "order": {
        "symbol": "AAPL",
        "side": "buy",
        "qty": "100"
      }
    }
  })";

  const TradeUpdate update = parseTradingUpdate(json);
  ASSERT_TRUE(std::holds_alternative<CancelEvent>(update));

  const auto &cancel = std::get<CancelEvent>(update);
  EXPECT_EQ(std::string_view(cancel.symbol, 4), "AAPL");
  EXPECT_EQ(cancel.side, OrderSide::Buy);
  EXPECT_EQ(cancel.quantity, 100);
}

TEST_F(FillListenerTest, ParsesExpiredEvent) {
  const std::string json = R"({
    "stream": "trade_updates",
    "data": {
      "event": "expired",
      "order": {
        "symbol": "AAPL",
        "side": "sell",
        "qty": "200"
      }
    }
  })";

  const TradeUpdate update = parseTradingUpdate(json);
  ASSERT_TRUE(std::holds_alternative<CancelEvent>(update));
}

TEST_F(FillListenerTest, ParsesRejectedEvent) {
  const std::string json = R"({
    "stream": "trade_updates",
    "data": {
      "event": "rejected",
      "order": {
        "symbol": "AAPL",
        "side": "buy",
        "qty": "50"
      }
    }
  })";

  const TradeUpdate update = parseTradingUpdate(json);
  ASSERT_TRUE(std::holds_alternative<CancelEvent>(update));
}

TEST_F(FillListenerTest, IgnoresNewEvent) {
  const std::string json = R"({
    "stream": "trade_updates",
    "data": {
      "event": "new",
      "order": {
        "symbol": "AAPL",
        "side": "buy",
        "qty": "100"
      }
    }
  })";

  const TradeUpdate update = parseTradingUpdate(json);
  EXPECT_TRUE(std::holds_alternative<std::monostate>(update));
}

TEST_F(FillListenerTest, IgnoresAuthorizationMessage) {
  const std::string json = R"({
    "stream": "authorization",
    "data": {
      "status": "authorized"
    }
  })";

  const TradeUpdate update = parseTradingUpdate(json);
  EXPECT_TRUE(std::holds_alternative<std::monostate>(update));
}

TEST_F(FillListenerTest, HandlesMalformedJson) {
  const TradeUpdate update = parseTradingUpdate("not valid json{{{");
  EXPECT_TRUE(std::holds_alternative<std::monostate>(update));
}

TEST_F(FillListenerTest, HandlesMissingFields) {
  const std::string json = R"({
    "stream": "trade_updates",
    "data": {}
  })";

  const TradeUpdate update = parseTradingUpdate(json);
  EXPECT_TRUE(std::holds_alternative<std::monostate>(update));
}

TEST_F(FillListenerTest, HandlesMissingQtyOnFill) {
  const std::string json = R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "price": "150.00",
      "order": {
        "symbol": "AAPL",
        "side": "buy"
      }
    }
  })";

  const TradeUpdate update = parseTradingUpdate(json);
  EXPECT_TRUE(std::holds_alternative<std::monostate>(update));
}

TEST_F(FillListenerTest, FillUpdatesPositionManager) {
  const std::string json = R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "100",
      "price": "150.25",
      "order": {
        "symbol": "AAPL",
        "side": "buy"
      }
    }
  })";

  // Simulate pending order first
  Order pending{};
  pending.id = 1;
  std::memcpy(pending.symbol, "AAPL\0\0\0\0", 8);
  pending.quantity = 100;
  pending.price = 1502500;
  pending.side = OrderSide::Buy;
  pending.type = OrderType::Limit;
  position_manager_->onOrderSent(pending);

  EXPECT_EQ(position_manager_->getPendingPosition("AAPL"), 100);
  EXPECT_EQ(position_manager_->getFilledPosition("AAPL"), 0);

  const TradeUpdate update = parseTradingUpdate(json);
  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));

  const auto &fill_event = std::get<FillEvent>(update);
  Fill fill{};
  std::memcpy(fill.symbol, fill_event.symbol, 8);
  fill.executionId = 0;
  fill.orderId = 0;
  fill.side = fill_event.side;
  fill.quantity = fill_event.quantity;
  fill.price = fill_event.price;
  position_manager_->onFill(fill);

  EXPECT_EQ(position_manager_->getPendingPosition("AAPL"), 0);
  EXPECT_EQ(position_manager_->getFilledPosition("AAPL"), 100);
}

TEST_F(FillListenerTest, CancelUpdatesPositionManager) {
  Order pending{};
  pending.id = 1;
  std::memcpy(pending.symbol, "AAPL\0\0\0\0", 8);
  pending.quantity = 100;
  pending.price = 1502500;
  pending.side = OrderSide::Buy;
  pending.type = OrderType::Limit;
  position_manager_->onOrderSent(pending);

  EXPECT_EQ(position_manager_->getPendingPosition("AAPL"), 100);

  const std::string json = R"({
    "stream": "trade_updates",
    "data": {
      "event": "canceled",
      "order": {
        "symbol": "AAPL",
        "side": "buy",
        "qty": "100"
      }
    }
  })";

  const TradeUpdate update = parseTradingUpdate(json);
  ASSERT_TRUE(std::holds_alternative<CancelEvent>(update));

  const auto &cancel_event = std::get<CancelEvent>(update);
  Order cancel_order{};
  cancel_order.id = 0;
  std::memcpy(cancel_order.symbol, cancel_event.symbol, 8);
  cancel_order.quantity = cancel_event.quantity;
  cancel_order.price = 0;
  cancel_order.side = cancel_event.side;
  cancel_order.type = OrderType::Market;
  position_manager_->onOrderCancelled(cancel_order);

  EXPECT_EQ(position_manager_->getPendingPosition("AAPL"), 0);
}

TEST_F(FillListenerTest, ParsesNumericQtyAndPrice) {
  const std::string json = R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": 75,
      "price": 99.50,
      "order": {
        "symbol": "AAPL",
        "side": "buy"
      }
    }
  })";

  const TradeUpdate update = parseTradingUpdate(json);
  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));

  const auto &fill = std::get<FillEvent>(update);
  EXPECT_EQ(fill.quantity, 75);
  EXPECT_DOUBLE_EQ(fill.price, 99.50);
}
