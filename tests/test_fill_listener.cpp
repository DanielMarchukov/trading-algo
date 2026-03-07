#include "AlpacaFillListener.hpp"
#include "PositionManager.hpp"
#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

class FillListenerTest : public ::testing::Test {
protected:
  void SetUp() override {
    position_manager_ = std::make_shared<PositionManager>();
    position_manager_->registerSymbol("AAPL");
    position_manager_->registerSymbol("GOOGL");
    listener_ = std::make_unique<AlpacaFillListener>(
        position_manager_, is_running_, "test_key", "test_secret");
  }

  static TradeUpdate parse(const std::string &json_str) {
    return parseTradingUpdate(nlohmann::json::parse(json_str));
  }

  void addPendingOrder(const char *symbol, int64_t qty, OrderSide side) {
    Order order{};
    order.id = 1;
    std::memcpy(order.symbol, symbol, strnlen(symbol, 8));
    order.quantity = qty;
    order.price = 1000000;
    order.side = side;
    order.type = OrderType::Limit;
    position_manager_->onOrderSent(order);
  }

  ix::WebSocketMessagePtr makeMessage(ix::WebSocketMessageType type,
                                      const std::string &body) {
    return std::make_unique<ix::WebSocketMessage>(
        type, body, body.size(), ix::WebSocketErrorInfo{},
        ix::WebSocketOpenInfo{}, ix::WebSocketCloseInfo{});
  }

  void callHandleTradeUpdate(const std::string &json) {
    listener_->handleTradeUpdate(json);
  }

  void callOnMessage(const ix::WebSocketMessagePtr &msg) {
    listener_->onMessage(msg);
  }

  std::shared_ptr<PositionManager> position_manager_;
  std::atomic<bool> is_running_{true};
  std::unique_ptr<AlpacaFillListener> listener_;
};

TEST_F(FillListenerTest, ParsesFillEvent) {
  const auto update = parse(R"({
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
  })");

  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));

  const auto &fill = std::get<FillEvent>(update);
  EXPECT_EQ(
      std::string_view(fill.symbol, strnlen(fill.symbol, sizeof(fill.symbol))),
      "AAPL");
  EXPECT_EQ(fill.side, OrderSide::Buy);
  EXPECT_EQ(fill.quantity, 100);
  EXPECT_DOUBLE_EQ(fill.price, 150.25);
}

TEST_F(FillListenerTest, ParsesPartialFillEvent) {
  const auto update = parse(R"({
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
  })");

  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));

  const auto &fill = std::get<FillEvent>(update);
  EXPECT_EQ(fill.side, OrderSide::Sell);
  EXPECT_EQ(fill.quantity, 50);
  EXPECT_DOUBLE_EQ(fill.price, 200.0);
}

TEST_F(FillListenerTest, ParsesCanceledEvent) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "canceled",
      "order": {
        "symbol": "AAPL",
        "side": "buy",
        "qty": "100",
        "filled_qty": "0"
      }
    }
  })");

  ASSERT_TRUE(std::holds_alternative<CancelEvent>(update));

  const auto &cancel = std::get<CancelEvent>(update);
  EXPECT_EQ(std::string_view(cancel.symbol,
                             strnlen(cancel.symbol, sizeof(cancel.symbol))),
            "AAPL");
  EXPECT_EQ(cancel.side, OrderSide::Buy);
  EXPECT_EQ(cancel.quantity, 100);
}

TEST_F(FillListenerTest, CancelAfterPartialFillUsesRemainingQty) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "canceled",
      "order": {
        "symbol": "AAPL",
        "side": "buy",
        "qty": "100",
        "filled_qty": "60"
      }
    }
  })");

  ASSERT_TRUE(std::holds_alternative<CancelEvent>(update));
  EXPECT_EQ(std::get<CancelEvent>(update).quantity, 40);
}

TEST_F(FillListenerTest, ParsesExpiredEvent) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "expired",
      "order": {
        "symbol": "AAPL",
        "side": "sell",
        "qty": "200",
        "filled_qty": "0"
      }
    }
  })");

  ASSERT_TRUE(std::holds_alternative<CancelEvent>(update));
}

TEST_F(FillListenerTest, ParsesRejectedEvent) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "rejected",
      "order": {
        "symbol": "AAPL",
        "side": "buy",
        "qty": "50",
        "filled_qty": "0"
      }
    }
  })");

  ASSERT_TRUE(std::holds_alternative<CancelEvent>(update));
}

TEST_F(FillListenerTest, IgnoresNewEvent) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "new",
      "order": {
        "symbol": "AAPL",
        "side": "buy",
        "qty": "100"
      }
    }
  })");

  EXPECT_TRUE(std::holds_alternative<std::monostate>(update));
}

TEST_F(FillListenerTest, IgnoresAuthorizationMessage) {
  const auto update = parse(R"({
    "stream": "authorization",
    "data": {
      "status": "authorized"
    }
  })");

  EXPECT_TRUE(std::holds_alternative<std::monostate>(update));
}

TEST_F(FillListenerTest, HandlesMalformedJson) {
  nlohmann::json broken = {{"not", "trade_updates"}};
  const TradeUpdate update = parseTradingUpdate(broken);
  EXPECT_TRUE(std::holds_alternative<std::monostate>(update));
}

TEST_F(FillListenerTest, HandlesMissingFields) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {}
  })");

  EXPECT_TRUE(std::holds_alternative<std::monostate>(update));
}

TEST_F(FillListenerTest, HandlesMissingQtyOnFill) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "price": "150.00",
      "order": {
        "symbol": "AAPL",
        "side": "buy"
      }
    }
  })");

  EXPECT_TRUE(std::holds_alternative<std::monostate>(update));
}

TEST_F(FillListenerTest, RejectsUnknownSide) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "100",
      "price": "150.00",
      "order": {
        "symbol": "AAPL",
        "side": "unknown"
      }
    }
  })");

  EXPECT_TRUE(std::holds_alternative<std::monostate>(update));
}

TEST_F(FillListenerTest, RejectsInvalidQtyString) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "not_a_number",
      "price": "150.00",
      "order": {
        "symbol": "AAPL",
        "side": "buy"
      }
    }
  })");

  EXPECT_TRUE(std::holds_alternative<std::monostate>(update));
}

TEST_F(FillListenerTest, FillUpdatesPositionManager) {
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

  const auto update = parse(R"({
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
  })");

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

  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "canceled",
      "order": {
        "symbol": "AAPL",
        "side": "buy",
        "qty": "100",
        "filled_qty": "0"
      }
    }
  })");

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
  const auto update = parse(R"({
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
  })");

  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));

  const auto &fill = std::get<FillEvent>(update);
  EXPECT_EQ(fill.quantity, 75);
  EXPECT_DOUBLE_EQ(fill.price, 99.50);
}

// --- handleTradeUpdate tests (full dispatch pipeline) ---

TEST_F(FillListenerTest, HandleTradeUpdateRejectsInvalidJson) {
  addPendingOrder("AAPL", 100, OrderSide::Buy);
  callHandleTradeUpdate("not valid json{{{");
  EXPECT_EQ(position_manager_->getPendingPosition("AAPL"), 100);
}

TEST_F(FillListenerTest, HandleTradeUpdateProcessesAuthSuccess) {
  callHandleTradeUpdate(R"({
    "stream": "authorization",
    "data": {"status": "authorized"}
  })");
  // sendSubscribe calls ws_.send on non-connected socket — no crash
}

TEST_F(FillListenerTest, HandleTradeUpdateProcessesAuthFailure) {
  callHandleTradeUpdate(R"({
    "stream": "authorization",
    "data": {"status": "unauthorized"}
  })");
}

TEST_F(FillListenerTest, HandleTradeUpdateProcessesListeningSuccess) {
  callHandleTradeUpdate(R"({
    "stream": "listening",
    "data": {"streams": ["trade_updates"]}
  })");
}

TEST_F(FillListenerTest, HandleTradeUpdateProcessesListeningMissingStream) {
  callHandleTradeUpdate(R"({
    "stream": "listening",
    "data": {"streams": ["account_updates"]}
  })");
}

TEST_F(FillListenerTest, HandleTradeUpdateProcessesMalformedListening) {
  callHandleTradeUpdate(R"({
    "stream": "listening",
    "data": {}
  })");
}

TEST_F(FillListenerTest, HandleTradeUpdateDispatchesFill) {
  addPendingOrder("AAPL", 100, OrderSide::Buy);

  callHandleTradeUpdate(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "100",
      "price": "150.25",
      "order": {"symbol": "AAPL", "side": "buy"}
    }
  })");

  EXPECT_EQ(position_manager_->getPendingPosition("AAPL"), 0);
  EXPECT_EQ(position_manager_->getFilledPosition("AAPL"), 100);
}

TEST_F(FillListenerTest, HandleTradeUpdateDispatchesCancel) {
  addPendingOrder("AAPL", 100, OrderSide::Buy);

  callHandleTradeUpdate(R"({
    "stream": "trade_updates",
    "data": {
      "event": "canceled",
      "order": {
        "symbol": "AAPL",
        "side": "buy",
        "qty": "100",
        "filled_qty": "0"
      }
    }
  })");

  EXPECT_EQ(position_manager_->getPendingPosition("AAPL"), 0);
}

TEST_F(FillListenerTest, HandleTradeUpdateIgnoresUnknownEvent) {
  addPendingOrder("AAPL", 100, OrderSide::Buy);

  callHandleTradeUpdate(R"({
    "stream": "trade_updates",
    "data": {
      "event": "new",
      "order": {"symbol": "AAPL", "side": "buy", "qty": "100"}
    }
  })");

  EXPECT_EQ(position_manager_->getPendingPosition("AAPL"), 100);
}

// --- onMessage tests (WebSocket dispatch) ---

TEST_F(FillListenerTest, OnMessageIgnoresWhenNotRunning) {
  addPendingOrder("AAPL", 100, OrderSide::Buy);
  is_running_.store(false);

  std::string body = R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "100",
      "price": "150.25",
      "order": {"symbol": "AAPL", "side": "buy"}
    }
  })";
  auto msg = makeMessage(ix::WebSocketMessageType::Message, body);
  callOnMessage(msg);

  EXPECT_EQ(position_manager_->getPendingPosition("AAPL"), 100);
  EXPECT_EQ(position_manager_->getFilledPosition("AAPL"), 0);
}

TEST_F(FillListenerTest, OnMessageDispatchesFillEvent) {
  addPendingOrder("AAPL", 50, OrderSide::Buy);

  std::string body = R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "50",
      "price": "200.00",
      "order": {"symbol": "AAPL", "side": "buy"}
    }
  })";
  auto msg = makeMessage(ix::WebSocketMessageType::Message, body);
  callOnMessage(msg);

  EXPECT_EQ(position_manager_->getPendingPosition("AAPL"), 0);
  EXPECT_EQ(position_manager_->getFilledPosition("AAPL"), 50);
}

TEST_F(FillListenerTest, OnMessageHandlesOpenType) {
  std::string body;
  auto msg = makeMessage(ix::WebSocketMessageType::Open, body);
  callOnMessage(msg);
  // sendAuth calls ws_.send on non-connected socket — no crash
}

TEST_F(FillListenerTest, OnMessageHandlesErrorType) {
  std::string body;
  auto msg = makeMessage(ix::WebSocketMessageType::Error, body);
  callOnMessage(msg);
}

TEST_F(FillListenerTest, OnMessageHandlesCloseType) {
  std::string body;
  auto msg = makeMessage(ix::WebSocketMessageType::Close, body);
  callOnMessage(msg);
}
