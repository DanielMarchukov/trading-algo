#include "AlpacaFillListener.hpp"
#include "IRestClient.hpp"
#include "LatencyTracker.hpp"
#include "PendingOrderTracker.hpp"
#include "PositionManager.hpp"
#include "TestHelpers.hpp"
#include "ThreadGuard.hpp"
#include <cstring>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>

class FillListenerTest : public ::testing::Test {
protected:
  void SetUp() override {
    position_manager_ = std::make_unique<PositionManager>();
    position_manager_->registerSymbol("AAPL");
    position_manager_->registerSymbol("GOOGL");
    listener_ = std::make_unique<AlpacaFillListener>(
        position_manager_.get(), is_running_, "test_key", "test_secret");
  }

  static TradeUpdate parse(const std::string &json_str) {
    return parseTradingUpdate(nlohmann::json::parse(json_str));
  }

  void addPendingOrder(const char *symbol, int64_t qty, OrderSide side) {
    Order order = test_helpers::createOrder(symbol, side, qty, 1000000);
    order.id = 1;
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

  std::unique_ptr<PositionManager> position_manager_;
  std::atomic<bool> is_running_{true};
  std::unique_ptr<AlpacaFillListener> listener_;
};

TEST(AlpacaFillListenerConstructionTest, ThrowsOnNullPositionManager) {
  std::atomic<bool> is_running{true};
  EXPECT_THROW(AlpacaFillListener(nullptr, is_running, "key", "secret"),
               std::invalid_argument);
}

struct FillParseParam {
  const char *event_type;
  const char *symbol;
  const char *side_str;
  const char *qty;
  const char *price;
  const char *order_id;
  OrderSide expected_side;
  int64_t expected_qty;
  double expected_price;
};

class FillParseTest : public FillListenerTest,
                      public ::testing::WithParamInterface<FillParseParam> {};

TEST_P(FillParseTest, ParsesFillFields) {
  const auto &[event_type, symbol, side_str, qty, price, order_id,
               expected_side, expected_qty, expected_price] = GetParam();
  const std::string json =
      R"({"stream":"trade_updates","data":{"event":")" +
      std::string(event_type) + R"(","qty":")" + qty + R"(","price":")" +
      price + R"(","order":{"symbol":")" + symbol + R"(","side":")" + side_str +
      R"(","id":")" + order_id + R"("}}})";
  const auto update = parse(json);

  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));

  const auto &fill = std::get<FillEvent>(update);
  EXPECT_EQ(
      std::string_view(fill.symbol, strnlen(fill.symbol, sizeof(fill.symbol))),
      symbol);
  EXPECT_EQ(fill.side, expected_side);
  EXPECT_EQ(fill.quantity, expected_qty);
  EXPECT_DOUBLE_EQ(fill.price, expected_price);
  EXPECT_EQ(std::string_view(
                fill.alpaca_order_id,
                strnlen(fill.alpaca_order_id, sizeof(fill.alpaca_order_id))),
            order_id);
  EXPECT_EQ(fill.is_partial, std::string(event_type) == "partial_fill");
}

INSTANTIATE_TEST_SUITE_P(
    FillTypes, FillParseTest,
    ::testing::Values(FillParseParam{"fill", "AAPL", "buy", "100", "150.25",
                                     "b0929f79-27a5-480f-9b94-e2f123456789",
                                     OrderSide::Buy, 100, 150.25},
                      FillParseParam{"partial_fill", "GOOGL", "sell", "50",
                                     "200.00",
                                     "c1234567-89ab-cdef-0123-456789abcdef",
                                     OrderSide::Sell, 50, 200.0}));

struct CancelParseParam {
  const char *event_type;
  const char *order_id;
};

class CancelEventTypeTest
    : public FillListenerTest,
      public ::testing::WithParamInterface<CancelParseParam> {};

TEST_P(CancelEventTypeTest, ProducesCancelEvent) {
  const auto &[event_type, order_id] = GetParam();
  const std::string json =
      R"({"stream":"trade_updates","data":{"event":")" +
      std::string(event_type) +
      R"(","order":{"symbol":"AAPL","side":"buy","qty":"100","filled_qty":"0","id":")" +
      order_id + R"("}}})";
  const auto update = parse(json);

  ASSERT_TRUE(std::holds_alternative<CancelEvent>(update));

  const auto &cancel = std::get<CancelEvent>(update);
  EXPECT_EQ(std::string_view(cancel.symbol,
                             strnlen(cancel.symbol, sizeof(cancel.symbol))),
            "AAPL");
  EXPECT_EQ(cancel.side, OrderSide::Buy);
  EXPECT_EQ(cancel.quantity, 100);
  EXPECT_EQ(std::string_view(cancel.alpaca_order_id,
                             strnlen(cancel.alpaca_order_id,
                                     sizeof(cancel.alpaca_order_id))),
            order_id);
}

INSTANTIATE_TEST_SUITE_P(
    EventTypes, CancelEventTypeTest,
    ::testing::Values(
        CancelParseParam{"canceled", "aaa11111-2222-3333-4444-555566667777"},
        CancelParseParam{"expired", "bbb11111-2222-3333-4444-555566667777"},
        CancelParseParam{"rejected", "ccc11111-2222-3333-4444-555566667777"}));

TEST_F(FillListenerTest, FillWithMissingOrderIdProducesEmptyId) {
  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "50",
      "price": "100.00",
      "order": {"symbol": "AAPL", "side": "buy"}
    }
  })");

  ASSERT_TRUE(std::holds_alternative<FillEvent>(update));
  const auto &fill = std::get<FillEvent>(update);
  EXPECT_EQ(std::string_view(
                fill.alpaca_order_id,
                strnlen(fill.alpaca_order_id, sizeof(fill.alpaca_order_id))),
            "");
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
        "filled_qty": "60",
        "id": "partial-cancel-id"
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
  std::ostringstream captured;
  auto *old_buf = std::cerr.rdbuf(captured.rdbuf());

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

  std::cerr.rdbuf(old_buf);
  EXPECT_TRUE(std::holds_alternative<std::monostate>(update));
  EXPECT_NE(captured.str().find("FillListener: parseQty failed:"),
            std::string::npos);
}

TEST_F(FillListenerTest, RejectsInvalidPriceString) {
  std::ostringstream captured;
  auto *old_buf = std::cerr.rdbuf(captured.rdbuf());

  const auto update = parse(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "100",
      "price": "not_a_price",
      "order": {
        "symbol": "AAPL",
        "side": "buy"
      }
    }
  })");

  std::cerr.rdbuf(old_buf);
  EXPECT_TRUE(std::holds_alternative<std::monostate>(update));
  EXPECT_NE(captured.str().find("FillListener: parsePrice failed:"),
            std::string::npos);
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
  std::promise<void> processor_finished;

  ThreadGuard processor{std::thread([&]() {
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
    processor_finished.set_value();
  })};

  while (processed.load(std::memory_order_relaxed) < 5) {
    std::this_thread::yield();
  }
  listener_->stop();

  auto status =
      processor_finished.get_future().wait_for(std::chrono::seconds(5));
  ASSERT_EQ(status, std::future_status::ready) << "timeout — possible deadlock";

  const int64_t filled = position_manager_->getFilledPosition("AAPL");
  const int64_t pending = position_manager_->getPendingPosition("AAPL");
  EXPECT_EQ(filled + pending, num_fills);
  EXPECT_EQ(filled, processed.load(std::memory_order_relaxed));
}

class FillListenerTrackerTest : public ::testing::Test {
protected:
  void SetUp() override {
    position_manager_ = std::make_unique<PositionManager>();
    position_manager_->registerSymbol("AAPL");
    tracker_ = std::make_unique<PendingOrderTracker>();
    listener_ = std::make_unique<AlpacaFillListener>(
        position_manager_.get(), is_running_, "test_key", "test_secret",
        tracker_.get());
  }

  void callHandleTradeUpdate(const std::string &json) {
    listener_->handleTradeUpdate(json);
  }

  std::unique_ptr<PositionManager> position_manager_;
  std::unique_ptr<PendingOrderTracker> tracker_;
  std::atomic<bool> is_running_{true};
  std::unique_ptr<AlpacaFillListener> listener_;
};

TEST_F(FillListenerTrackerTest, FillNotifiesTracker) {
  SymbolKey key{};
  std::memcpy(key.value, "AAPL", 4);
  tracker_->recordOrder(key, OrderSide::Buy, "fill-order-id");

  Order order = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  order.id = 1;
  position_manager_->onOrderSent(order);

  callHandleTradeUpdate(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "100",
      "price": "150.25",
      "order": {"symbol": "AAPL", "side": "buy", "id": "fill-order-id"}
    }
  })");

  EXPECT_FALSE(tracker_->getExistingOrder(key, OrderSide::Buy).has_value());
}

TEST_F(FillListenerTrackerTest, PartialFillDoesNotClearTracker) {
  SymbolKey key{};
  std::memcpy(key.value, "AAPL", 4);
  tracker_->recordOrder(key, OrderSide::Buy, "partial-order-id");

  Order order = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  order.id = 1;
  position_manager_->onOrderSent(order);

  callHandleTradeUpdate(R"({
    "stream": "trade_updates",
    "data": {
      "event": "partial_fill",
      "qty": "50",
      "price": "150.25",
      "order": {"symbol": "AAPL", "side": "buy", "id": "partial-order-id"}
    }
  })");

  EXPECT_TRUE(tracker_->getExistingOrder(key, OrderSide::Buy).has_value());
}

TEST_F(FillListenerTrackerTest, FillRecordsLatencyWhenTrackerProvided) {
  auto latency_tracker = std::make_unique<LatencyTracker>();
  latency_tracker->recordOrderSubmit("latency-order-id");

  listener_ = std::make_unique<AlpacaFillListener>(
      position_manager_.get(), is_running_, "test_key", "test_secret",
      tracker_.get(), latency_tracker.get());

  Order order = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  order.id = 1;
  position_manager_->onOrderSent(order);

  SymbolKey key{};
  std::memcpy(key.value, "AAPL", 4);
  tracker_->recordOrder(key, OrderSide::Buy, "latency-order-id");

  callHandleTradeUpdate(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "100",
      "price": "150.25",
      "order": {"symbol": "AAPL", "side": "buy", "id": "latency-order-id"}
    }
  })");

  EXPECT_EQ(latency_tracker->count(LatencyMetric::FillRoundTrip), 1);
  EXPECT_GT(latency_tracker->percentile(LatencyMetric::FillRoundTrip, 50.0), 0);
}

TEST_F(FillListenerTrackerTest, CancelNotifiesTracker) {
  SymbolKey key{};
  std::memcpy(key.value, "AAPL", 4);
  tracker_->recordOrder(key, OrderSide::Buy, "cancel-order-id");

  Order order = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  order.id = 1;
  position_manager_->onOrderSent(order);

  callHandleTradeUpdate(R"({
    "stream": "trade_updates",
    "data": {
      "event": "canceled",
      "order": {
        "symbol": "AAPL",
        "side": "buy",
        "qty": "100",
        "filled_qty": "0",
        "id": "cancel-order-id"
      }
    }
  })");

  EXPECT_FALSE(tracker_->getExistingOrder(key, OrderSide::Buy).has_value());
}

// --- Reconciliation tests ---

class MockReconciliationClient final : public IRestClient {
public:
  std::expected<OrderAck, OrderError>
  placeOrder(const Order & /*order*/) override {
    return OrderAck{"mock-id", "accepted"};
  }

  std::expected<void, OrderError>
  cancelOrder(std::string_view /*alpaca_order_id*/) override {
    return {};
  }

  std::expected<std::vector<AlpacaOrderStatus>, OrderError>
  queryOrders(std::string_view /*status_filter*/,
              std::string_view /*after*/) override {
    return orders_to_return;
  }

  std::vector<AlpacaOrderStatus> orders_to_return;
};

class FillListenerReconcileTest : public ::testing::Test {
protected:
  void SetUp() override {
    position_manager_ = std::make_unique<PositionManager>();
    position_manager_->registerSymbol("AAPL");
    position_manager_->registerSymbol("GOOGL");
    tracker_ = std::make_unique<PendingOrderTracker>();
    mock_client_ = std::make_unique<MockReconciliationClient>();
    listener_ = std::make_unique<AlpacaFillListener>(
        position_manager_.get(), is_running_, "test_key", "test_secret",
        tracker_.get(), nullptr, mock_client_.get());
  }

  void simulateReconnect() {
    listener_->has_connected_ = true;
    listener_->reconcileAfterReconnect();
  }

  void callHandleTradeUpdate(const std::string &json) {
    listener_->handleTradeUpdate(json);
  }

  std::unique_ptr<PositionManager> position_manager_;
  std::unique_ptr<PendingOrderTracker> tracker_;
  std::unique_ptr<MockReconciliationClient> mock_client_;
  std::atomic<bool> is_running_{true};
  std::unique_ptr<AlpacaFillListener> listener_;
};

TEST_F(FillListenerReconcileTest, ReconcilesMissedFill) {
  Order order = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  order.id = 1;
  position_manager_->onOrderSent(order);

  SymbolKey key{};
  std::memcpy(key.value, "AAPL", 4);
  tracker_->recordOrder(key, OrderSide::Buy, "missed-fill-id");

  mock_client_->orders_to_return = {
      {"missed-fill-id", "AAPL", "buy", "filled", "", 100, 100, 150.25}};

  simulateReconnect();

  EXPECT_EQ(position_manager_->getFilledPosition("AAPL"), 100);
  EXPECT_FALSE(tracker_->getExistingOrder(key, OrderSide::Buy).has_value());
}

struct TerminalCancelParam {
  const char *status;
};

class ReconcileTerminalCancelTest
    : public FillListenerReconcileTest,
      public ::testing::WithParamInterface<TerminalCancelParam> {};

TEST_P(ReconcileTerminalCancelTest, ClearsPositionAndTracker) {
  const auto &[status] = GetParam();
  Order order = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  order.id = 1;
  position_manager_->onOrderSent(order);

  SymbolKey key{};
  std::memcpy(key.value, "AAPL", 4);
  tracker_->recordOrder(key, OrderSide::Buy, "terminal-order-id");

  mock_client_->orders_to_return = {
      {"terminal-order-id", "AAPL", "buy", status, "", 100, 0, 0.0}};

  simulateReconnect();

  EXPECT_EQ(position_manager_->getPendingPosition("AAPL"), 0);
  EXPECT_FALSE(tracker_->getExistingOrder(key, OrderSide::Buy).has_value());
}

INSTANTIATE_TEST_SUITE_P(TerminalStatuses, ReconcileTerminalCancelTest,
                         ::testing::Values(TerminalCancelParam{"canceled"},
                                           TerminalCancelParam{"expired"},
                                           TerminalCancelParam{"rejected"}));

TEST_F(FillListenerReconcileTest, SkipsAlreadyProcessedFill) {
  Order order = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  order.id = 1;
  position_manager_->onOrderSent(order);

  callHandleTradeUpdate(R"({
    "stream": "trade_updates",
    "data": {
      "event": "fill",
      "qty": "100",
      "price": "150.25",
      "order": {"symbol": "AAPL", "side": "buy", "id": "already-filled-id"}
    }
  })");

  EXPECT_EQ(position_manager_->getFilledPosition("AAPL"), 100);

  mock_client_->orders_to_return = {
      {"already-filled-id", "AAPL", "buy", "filled", "", 100, 100, 150.25}};

  simulateReconnect();

  EXPECT_EQ(position_manager_->getFilledPosition("AAPL"), 100);
}

TEST_F(FillListenerReconcileTest, ReconcilesMissedPartialFillRemainder) {
  Order order = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  order.id = 1;
  position_manager_->onOrderSent(order);

  callHandleTradeUpdate(R"({
    "stream": "trade_updates",
    "data": {
      "event": "partial_fill",
      "qty": "60",
      "price": "150.00",
      "order": {"symbol": "AAPL", "side": "buy", "id": "partial-order-id"}
    }
  })");

  EXPECT_EQ(position_manager_->getFilledPosition("AAPL"), 60);

  mock_client_->orders_to_return = {
      {"partial-order-id", "AAPL", "buy", "filled", "", 100, 100, 150.40}};

  simulateReconnect();

  EXPECT_EQ(position_manager_->getFilledPosition("AAPL"), 100);
}

TEST_F(FillListenerReconcileTest, HandlesQueryFailure) {
  class FailingClient final : public IRestClient {
  public:
    std::expected<OrderAck, OrderError>
    placeOrder(const Order & /*order*/) override {
      return OrderAck{"mock-id", "accepted"};
    }
    std::expected<void, OrderError>
    cancelOrder(std::string_view /*alpaca_order_id*/) override {
      return {};
    }
    std::expected<std::vector<AlpacaOrderStatus>, OrderError>
    queryOrders(std::string_view /*status_filter*/,
                std::string_view /*after*/) override {
      return std::unexpected(OrderError{500, "Internal Server Error"});
    }
  };

  auto fail_client = std::make_unique<FailingClient>();
  listener_ = std::make_unique<AlpacaFillListener>(
      position_manager_.get(), is_running_, "test_key", "test_secret",
      tracker_.get(), nullptr, fail_client.get());

  EXPECT_NO_THROW(simulateReconnect());
}

TEST_F(FillListenerReconcileTest, SkipsOrdersWithUnknownSide) {
  mock_client_->orders_to_return = {
      {"unknown-side-id", "AAPL", "short", "filled", "", 100, 100, 150.25}};

  simulateReconnect();

  EXPECT_EQ(position_manager_->getFilledPosition("AAPL"), 0);
}

TEST_F(FillListenerReconcileTest, ReconcilesSellFill) {
  Order order = test_helpers::createOrder("AAPL", OrderSide::Sell, 50, 0);
  order.id = 1;
  position_manager_->onOrderSent(order);

  SymbolKey key{};
  std::memcpy(key.value, "AAPL", 4);
  tracker_->recordOrder(key, OrderSide::Sell, "sell-fill-id");

  mock_client_->orders_to_return = {
      {"sell-fill-id", "AAPL", "sell", "filled", "", 50, 50, 155.00}};

  simulateReconnect();

  EXPECT_EQ(position_manager_->getFilledPosition("AAPL"), -50);
  EXPECT_FALSE(tracker_->getExistingOrder(key, OrderSide::Sell).has_value());
}

TEST_F(FillListenerReconcileTest, ReconcilesCancelAfterPartialFill) {
  Order order = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  order.id = 1;
  position_manager_->onOrderSent(order);

  mock_client_->orders_to_return = {
      {"cancel-partial-id", "AAPL", "buy", "canceled", "", 100, 40, 149.50}};

  simulateReconnect();

  EXPECT_EQ(position_manager_->getFilledPosition("AAPL"), 40);
  EXPECT_EQ(position_manager_->getPendingPosition("AAPL"), 0);
}

TEST_F(FillListenerReconcileTest, NoReconciliationWithoutClient) {
  listener_ = std::make_unique<AlpacaFillListener>(
      position_manager_.get(), is_running_, "test_key", "test_secret",
      tracker_.get());

  EXPECT_NO_THROW(simulateReconnect());
}

TEST_F(FillListenerReconcileTest, ReconcileMultipleOrders) {
  Order buy_order = test_helpers::createOrder("AAPL", OrderSide::Buy, 100, 0);
  buy_order.id = 1;
  position_manager_->onOrderSent(buy_order);

  Order sell_order = test_helpers::createOrder("GOOGL", OrderSide::Sell, 50, 0);
  sell_order.id = 2;
  position_manager_->onOrderSent(sell_order);

  SymbolKey aapl_key{};
  std::memcpy(aapl_key.value, "AAPL", 4);
  tracker_->recordOrder(aapl_key, OrderSide::Buy, "aapl-fill-id");

  SymbolKey googl_key{};
  std::memcpy(googl_key.value, "GOOGL", 5);
  tracker_->recordOrder(googl_key, OrderSide::Sell, "googl-cancel-id");

  mock_client_->orders_to_return = {
      {"aapl-fill-id", "AAPL", "buy", "filled", "", 100, 100, 150.25},
      {"googl-cancel-id", "GOOGL", "sell", "canceled", "", 50, 0, 0.0}};

  simulateReconnect();

  EXPECT_EQ(position_manager_->getFilledPosition("AAPL"), 100);
  EXPECT_EQ(position_manager_->getPendingPosition("GOOGL"), 0);
  EXPECT_FALSE(
      tracker_->getExistingOrder(aapl_key, OrderSide::Buy).has_value());
  EXPECT_FALSE(
      tracker_->getExistingOrder(googl_key, OrderSide::Sell).has_value());
}
