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

struct FillParseParam {
  const char *event_type;
  const char *symbol;
  const char *side_str;
  const char *qty;
  const char *price;
  OrderSide expected_side;
  int64_t expected_qty;
  double expected_price;
};

class FillParseTest : public FillListenerTest,
                      public ::testing::WithParamInterface<FillParseParam> {};

TEST_P(FillParseTest, ParsesFillFields) {
  const auto &[event_type, symbol, side_str, qty, price, expected_side,
               expected_qty, expected_price] = GetParam();
  const std::string json = R"({"stream":"trade_updates","data":{"event":")" +
                           std::string(event_type) + R"(","qty":")" + qty +
                           R"(","price":")" + price +
                           R"(","order":{"symbol":")" + symbol +
                           R"(","side":")" + side_str + R"("}}})";
  const auto update = parse(json);

  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));

  const auto &fill = std::get<FillEvent>(update);
  EXPECT_EQ(
      std::string_view(fill.symbol, strnlen(fill.symbol, sizeof(fill.symbol))),
      symbol);
  EXPECT_EQ(fill.side, expected_side);
  EXPECT_EQ(fill.quantity, expected_qty);
  EXPECT_DOUBLE_EQ(fill.price, expected_price);
}

INSTANTIATE_TEST_SUITE_P(
    FillTypes, FillParseTest,
    ::testing::Values(FillParseParam{"fill", "AAPL", "buy", "100", "150.25",
                                     OrderSide::Buy, 100, 150.25},
                      FillParseParam{"partial_fill", "GOOGL", "sell", "50",
                                     "200.00", OrderSide::Sell, 50, 200.0}));

class CancelEventTypeTest : public FillListenerTest,
                            public ::testing::WithParamInterface<const char *> {
};

TEST_P(CancelEventTypeTest, ProducesCancelEvent) {
  const std::string json =
      R"({"stream":"trade_updates","data":{"event":")" +
      std::string(GetParam()) +
      R"(","order":{"symbol":"AAPL","side":"buy","qty":"100","filled_qty":"0"}}})";
  const auto update = parse(json);

  ASSERT_TRUE(std::holds_alternative<CancelEvent>(update));

  const auto &cancel = std::get<CancelEvent>(update);
  EXPECT_EQ(std::string_view(cancel.symbol,
                             strnlen(cancel.symbol, sizeof(cancel.symbol))),
            "AAPL");
  EXPECT_EQ(cancel.side, OrderSide::Buy);
  EXPECT_EQ(cancel.quantity, 100);
}

INSTANTIATE_TEST_SUITE_P(EventTypes, CancelEventTypeTest,
                         ::testing::Values("canceled", "expired", "rejected"));

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

TEST_F(FillListenerTest, StopDuringActiveProcessingCompletesCorrectly) {
  constexpr int num_fills = 100;

  for (int i = 0; i < num_fills; ++i) {
    addPendingOrder("AAPL", 1, OrderSide::Buy);
  }

  std::atomic<int> processed{0};
  std::thread processor([&]() {
    for (int i = 0; i < num_fills; ++i) {
      callHandleTradeUpdate(R"({
        "stream": "trade_updates",
        "data": {
          "event": "fill",
          "qty": "1",
          "price": "150.25",
          "order": {"symbol": "AAPL", "side": "buy"}
        }
      })");
      processed.fetch_add(1, std::memory_order_relaxed);
    }
  });

  // Wait until processing has started, then stop
  while (processed.load(std::memory_order_relaxed) < 5) {
    std::this_thread::yield();
  }
  listener_->stop();

  processor.join();

  // All fills that were dispatched must have completed atomically —
  // no partial state corruption
  const int64_t filled = position_manager_->getFilledPosition("AAPL");
  const int64_t pending = position_manager_->getPendingPosition("AAPL");
  EXPECT_EQ(filled + pending, num_fills);
  EXPECT_EQ(filled, processed.load(std::memory_order_relaxed));
}

// --- Boundary condition tests ---

TEST_F(FillListenerTest, ParsesMaxLengthSymbol) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "100",
      "price": "150.25",
      "order": {"symbol": "ABCDEFGH", "side": "buy"}
    }
  })");

  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));
  const auto &fill = std::get<FillEvent>(update);
  EXPECT_EQ(
      std::string_view(fill.symbol, strnlen(fill.symbol, sizeof(fill.symbol))),
      "ABCDEFGH");
}

TEST_F(FillListenerTest, TruncatesOverLongSymbol) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "100",
      "price": "150.25",
      "order": {"symbol": "ABCDEFGHIJKLMNOP", "side": "buy"}
    }
  })");

  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));
  const auto &fill = std::get<FillEvent>(update);
  const auto symbol_len = strnlen(fill.symbol, sizeof(fill.symbol));
  EXPECT_LE(symbol_len, 8);
}

TEST_F(FillListenerTest, ParsesZeroQuantityFill) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "0",
      "price": "150.25",
      "order": {"symbol": "AAPL", "side": "buy"}
    }
  })");

  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));
  EXPECT_EQ(std::get<FillEvent>(update).quantity, 0);
}

TEST_F(FillListenerTest, ParsesVeryLargeQuantity) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "9223372036854775807",
      "price": "150.25",
      "order": {"symbol": "AAPL", "side": "buy"}
    }
  })");

  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));
  EXPECT_EQ(std::get<FillEvent>(update).quantity, 9223372036854775807LL);
}

TEST_F(FillListenerTest, ParsesVerySmallPrice) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "100",
      "price": "0.0001",
      "order": {"symbol": "AAPL", "side": "buy"}
    }
  })");

  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));
  EXPECT_DOUBLE_EQ(std::get<FillEvent>(update).price, 0.0001);
}

TEST_F(FillListenerTest, ParsesVeryLargePrice) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "100",
      "price": "999999.99",
      "order": {"symbol": "AAPL", "side": "buy"}
    }
  })");

  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));
  EXPECT_DOUBLE_EQ(std::get<FillEvent>(update).price, 999999.99);
}

TEST_F(FillListenerTest, ParsesZeroPrice) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "100",
      "price": "0.0",
      "order": {"symbol": "AAPL", "side": "buy"}
    }
  })");

  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));
  EXPECT_DOUBLE_EQ(std::get<FillEvent>(update).price, 0.0);
}

TEST_F(FillListenerTest, ParsesCancelWithZeroFilledQty) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "canceled",
      "order": {
        "symbol": "AAPL",
        "side": "buy",
        "qty": "0",
        "filled_qty": "0"
      }
    }
  })");

  ASSERT_TRUE(std::holds_alternative<CancelEvent>(update));
  EXPECT_EQ(std::get<CancelEvent>(update).quantity, 0);
}

TEST_F(FillListenerTest, ParsesCancelWithMissingQtyFields) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "canceled",
      "order": {
        "symbol": "AAPL",
        "side": "buy"
      }
    }
  })");

  ASSERT_TRUE(std::holds_alternative<CancelEvent>(update));
  EXPECT_EQ(std::get<CancelEvent>(update).quantity, 0);
}

TEST_F(FillListenerTest, ParsesNegativeQuantityInvalid) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "-100",
      "price": "150.25",
      "order": {"symbol": "AAPL", "side": "buy"}
    }
  })");

  // Negative quantity should still parse as it's valid int64_t
  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));
  EXPECT_EQ(std::get<FillEvent>(update).quantity, -100);
}

TEST_F(FillListenerTest, ParsesEmptySymbol) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "100",
      "price": "150.25",
      "order": {"symbol": "", "side": "buy"}
    }
  })");

  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));
  const auto &fill = std::get<FillEvent>(update);
  EXPECT_EQ(strnlen(fill.symbol, sizeof(fill.symbol)), 0);
}

TEST_F(FillListenerTest, ParsesScientificNotationPrice) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "100",
      "price": "1.5e2",
      "order": {"symbol": "AAPL", "side": "buy"}
    }
  })");

  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));
  EXPECT_DOUBLE_EQ(std::get<FillEvent>(update).price, 150.0);
}

TEST_F(FillListenerTest, HandlesConcurrentMessageProcessing) {
  constexpr int num_messages = 100;

  for (int i = 0; i < num_messages; ++i) {
    addPendingOrder("AAPL", 1, OrderSide::Buy);
  }

  std::atomic<bool> start{false};
  std::vector<std::thread> threads;

  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&, t]() {
      while (!start.load(std::memory_order_acquire)) {
      }
      for (int i = 0; i < num_messages / 4; ++i) {
        callHandleTradeUpdate(R"({
          "stream": "trade_updates",
          "data": {
            "event": "fill",
            "qty": "1",
            "price": "150.25",
            "order": {"symbol": "AAPL", "side": "buy"}
          }
        })");
      }
    });
  }

  start.store(true, std::memory_order_release);

  for (auto &t : threads) {
    t.join();
  }

  EXPECT_EQ(position_manager_->getFilledPosition("AAPL"), num_messages);
}