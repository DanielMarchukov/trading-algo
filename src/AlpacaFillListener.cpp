#include "AlpacaFillListener.hpp"
#include "IRestClient.hpp"
#include "LatencyTracker.hpp"
#include "PendingOrderTracker.hpp"
#include "ThreadPinning.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <nlohmann/json.hpp>
#include <optional>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <thread>

namespace {

constexpr int kMinReconnectMs = 100;
constexpr int kMaxReconnectMs = 30000;
constexpr std::size_t kSymbolCapacity = 8;
constexpr std::size_t kOrderIdCapacity = 48;

void copySymbol(char (&dest)[8], const std::string &src) {
  std::memset(dest, 0, kSymbolCapacity);
  const auto len = (std::min)(src.size(), kSymbolCapacity);
  if (len > 0) {
    std::memcpy(dest, src.data(), len);
  }
}

void copyOrderId(char (&dest)[48], const std::string &src) {
  std::memset(dest, 0, kOrderIdCapacity);
  const auto len = (std::min)(src.size(), kOrderIdCapacity);
  if (len > 0) {
    std::memcpy(dest, src.data(), len);
  }
}

[[nodiscard]] std::optional<OrderSide> parseSide(const std::string &side_str) {
  if (side_str == "buy") {
    return OrderSide::Buy;
  }
  if (side_str == "sell") {
    return OrderSide::Sell;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<int64_t> parseQty(const nlohmann::json &val) {
  try {
    if (val.is_string()) {
      return std::stoll(val.get<std::string>());
    }
    if (val.is_number_integer()) {
      return val.get<int64_t>();
    }
  } catch (const std::exception &e) {
    spdlog::get("fills")->warn("parseQty failed: {}", e.what());
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<double> parsePrice(const nlohmann::json &val) {
  try {
    if (val.is_string()) {
      return std::stod(val.get<std::string>());
    }
    if (val.is_number()) {
      return val.get<double>();
    }
  } catch (const std::exception &e) {
    spdlog::get("fills")->warn("parsePrice failed: {}", e.what());
  }
  return std::nullopt;
}

[[nodiscard]] std::string nowIso8601() {
  const auto now = std::chrono::system_clock::now();
  const auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32]{};
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

} // namespace

TradeUpdate parseTradingUpdate(const nlohmann::json &parsed) {
  if (!parsed.contains("stream") || parsed["stream"] != "trade_updates") {
    return std::monostate{};
  }

  if (!parsed.contains("data") || !parsed["data"].is_object()) {
    return std::monostate{};
  }

  const auto &data = parsed["data"];
  if (!data.contains("event") || !data["event"].is_string()) {
    return std::monostate{};
  }

  const std::string event = data["event"];

  if (!data.contains("order") || !data["order"].is_object()) {
    return std::monostate{};
  }

  const auto &order = data["order"];
  if (!order.contains("symbol") || !order["symbol"].is_string() ||
      !order.contains("side") || !order["side"].is_string()) {
    return std::monostate{};
  }

  const std::string symbol = order["symbol"];
  const auto side = parseSide(order["side"].get<std::string>());
  if (!side.has_value()) {
    return std::monostate{};
  }

  std::string order_id;
  if (order.contains("id") && order["id"].is_string()) {
    order_id = order["id"].get<std::string>();
  }

  if (event == "fill" || event == "partial_fill") {
    if (!data.contains("qty") || !data.contains("price")) {
      return std::monostate{};
    }

    const auto qty = parseQty(data["qty"]);
    const auto price = parsePrice(data["price"]);
    if (!qty.has_value() || !price.has_value()) {
      return std::monostate{};
    }

    FillEvent fill{};
    copySymbol(fill.symbol, symbol);
    fill.side = *side;
    fill.is_partial = (event == "partial_fill");
    fill.quantity = *qty;
    fill.price = *price;
    copyOrderId(fill.alpaca_order_id, order_id);
    return fill;
  }

  if (event == "canceled" || event == "expired" || event == "rejected") {
    CancelEvent cancel{};
    copySymbol(cancel.symbol, symbol);
    cancel.side = *side;
    copyOrderId(cancel.alpaca_order_id, order_id);

    int64_t total_qty = 0;
    int64_t filled_qty = 0;

    if (order.contains("qty")) {
      const auto q = parseQty(order["qty"]);
      if (q.has_value()) {
        total_qty = *q;
      }
    }
    if (order.contains("filled_qty")) {
      const auto fq = parseQty(order["filled_qty"]);
      if (fq.has_value()) {
        filled_qty = *fq;
      }
    }

    cancel.quantity = (std::max)(int64_t{0}, total_qty - filled_qty);
    return cancel;
  }

  return std::monostate{};
}

AlpacaFillListener::AlpacaFillListener(
    PositionManager *position_manager, std::atomic<bool> &is_running,
    const std::string &api_key, const std::string &api_secret,
    PendingOrderTracker *pending_tracker, LatencyTracker *latency_tracker,
    IRestClient *reconciliation_client, std::string fill_stream_url)
    : position_manager_(position_manager), pending_tracker_(pending_tracker),
      latency_tracker_(latency_tracker),
      reconciliation_client_(reconciliation_client), is_running_(is_running),
      api_key_(api_key), api_secret_(api_secret),
      fill_stream_url_(std::move(fill_stream_url)),
      logger_(spdlog::get("fills").get()) {
  if (position_manager_ == nullptr) {
    throw std::invalid_argument(
        "AlpacaFillListener: position_manager must not be null");
  }
}

AlpacaFillListener::~AlpacaFillListener() { AlpacaFillListener::stop(); }

void AlpacaFillListener::start() {
  ws_.setUrl(fill_stream_url_);
  ws_.setMinWaitBetweenReconnectionRetries(kMinReconnectMs);
  ws_.setMaxWaitBetweenReconnectionRetries(kMaxReconnectMs);

  ws_.setOnMessageCallback(
      [this](const ix::WebSocketMessagePtr &msg) { onMessage(msg); });

  thread_ = std::thread(&ix::WebSocket::run, &ws_);
  if (pin_thread_to_core(thread_, 0)) {
    logger_->info("Pinned FillListener thread to CPU Core 0");
  } else {
    logger_->warn("Failed to pin FillListener thread to CPU Core 0");
  }
}

void AlpacaFillListener::stop() {
  ws_.stop();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void AlpacaFillListener::onMessage(const ix::WebSocketMessagePtr &msg) {
  if (!is_running_.load()) {
    return;
  }

  switch (msg->type) {
  case ix::WebSocketMessageType::Open:
    if (has_connected_) {
      logger_->info("WebSocket reconnected — will reconcile after auth");
    } else {
      logger_->info("WebSocket connected");
    }
    sendAuth();
    break;

  case ix::WebSocketMessageType::Message:
    handleTradeUpdate(msg->str);
    break;

  case ix::WebSocketMessageType::Error:
    logger_->error("WebSocket error: {}", msg->errorInfo.reason);
    break;

  case ix::WebSocketMessageType::Close:
    logger_->info("WebSocket closed (code={})", msg->closeInfo.code);
    break;

  default:
    break;
  }
}

void AlpacaFillListener::sendAuth() {
  nlohmann::json auth_msg = {
      {"action", "auth"}, {"key", api_key_}, {"secret", api_secret_}};
  ws_.send(auth_msg.dump());
}

void AlpacaFillListener::sendSubscribe() {
  nlohmann::json sub_msg = {{"action", "listen"},
                            {"data", {{"streams", {"trade_updates"}}}}};
  ws_.send(sub_msg.dump());
}

void AlpacaFillListener::handleTradeUpdate(const std::string &json) {
  nlohmann::json parsed;
  try {
    parsed = nlohmann::json::parse(json);
  } catch (const nlohmann::json::exception &) {
    logger_->error("Failed to parse message");
    return;
  }

  if (parsed.contains("stream") && parsed["stream"] == "authorization") {
    handleAuthorization(parsed);
    return;
  }

  if (parsed.contains("stream") && parsed["stream"] == "listening") {
    handleListening(parsed);
    return;
  }

  const TradeUpdate update = parseTradingUpdate(parsed);

  if (std::holds_alternative<FillEvent>(update)) {
    processFillEvent(std::get<FillEvent>(update));
  } else if (std::holds_alternative<CancelEvent>(update)) {
    processCancelEvent(std::get<CancelEvent>(update));
  }
}

void AlpacaFillListener::handleAuthorization(const nlohmann::json &parsed) {
  if (parsed.contains("data") && parsed["data"].contains("status") &&
      parsed["data"]["status"] == "authorized") {
    logger_->info("Authorized");
    sendSubscribe();
    if (has_connected_) {
      reconcileAfterReconnect();
    } else {
      session_start_iso_ = nowIso8601();
    }
    has_connected_ = true;
  } else {
    logger_->error("Authorization failed");
  }
}

void AlpacaFillListener::handleListening(const nlohmann::json &parsed) {
  if (!parsed.contains("data") || !parsed["data"].contains("streams") ||
      !parsed["data"]["streams"].is_array()) {
    logger_->warn("Malformed listening response");
    return;
  }

  const auto &streams = parsed["data"]["streams"];
  bool found = false;
  for (const auto &s : streams) {
    if (s.is_string() && s.get<std::string>() == "trade_updates") {
      found = true;
      break;
    }
  }
  if (found) {
    logger_->info("Subscribed to trade_updates");
  } else {
    logger_->warn("trade_updates not in streams list");
  }
}

void AlpacaFillListener::processFillEvent(const FillEvent &fill_event) {
  Fill fill{};
  std::memcpy(fill.symbol, fill_event.symbol, kSymbolCapacity);
  fill.executionId = 0;
  fill.orderId = 0;
  fill.side = fill_event.side;
  fill.quantity = fill_event.quantity;
  fill.price = fill_event.price;
  position_manager_->onFill(fill);

  std::string_view order_id(
      fill_event.alpaca_order_id,
      strnlen(fill_event.alpaca_order_id, kOrderIdCapacity));
  if (!order_id.empty()) {
    upsertFilledQty(order_id, findFilledQty(order_id) + fill_event.quantity);
    if (!fill_event.is_partial) {
      markCompleted(order_id);
    }
  }
  if (latency_tracker_ && !order_id.empty()) {
    latency_tracker_->recordFillReceived(order_id);
  }
  if (pending_tracker_ && !fill_event.is_partial) {
    SymbolKey key{};
    std::memcpy(key.value, fill_event.symbol, kSymbolCapacity);
    pending_tracker_->onOrderCompleted(key, fill_event.side, order_id);
  }
  logger_->info("{} processed ({} qty={})",
                fill_event.is_partial ? "Partial fill" : "Fill",
                std::string_view(fill_event.symbol,
                                 strnlen(fill_event.symbol, kSymbolCapacity)),
                fill_event.quantity);
}

void AlpacaFillListener::processCancelEvent(const CancelEvent &cancel_event) {
  Order order{};
  order.id = 0;
  std::memcpy(order.symbol, cancel_event.symbol, kSymbolCapacity);
  order.quantity = cancel_event.quantity;
  order.price = 0;
  order.side = cancel_event.side;
  order.type = OrderType::Market;
  position_manager_->onOrderCancelled(order);

  std::string_view order_id(
      cancel_event.alpaca_order_id,
      strnlen(cancel_event.alpaca_order_id, kOrderIdCapacity));
  if (!order_id.empty()) {
    markCompleted(order_id);
  }
  if (pending_tracker_) {
    SymbolKey key{};
    std::memcpy(key.value, cancel_event.symbol, kSymbolCapacity);
    pending_tracker_->onOrderCompleted(key, cancel_event.side, order_id);
  }
  logger_->info("Cancel processed ({} qty={})",
                std::string_view(cancel_event.symbol,
                                 strnlen(cancel_event.symbol, kSymbolCapacity)),
                cancel_event.quantity);
}

void AlpacaFillListener::reconcileAfterReconnect() {
  if (reconciliation_client_ == nullptr) {
    logger_->warn(
        "No reconciliation client — skipping post-reconnect reconciliation");
    return;
  }

  logger_->info("Starting post-reconnect reconciliation");

  constexpr int64_t kPageSize = 500;
  int64_t reconciled_fills = 0;
  int64_t reconciled_cancels = 0;
  std::string cursor = session_start_iso_;

  for (;;) {
    auto result = reconciliation_client_->queryOrders("all", cursor);
    if (!result) {
      logger_->error("Reconciliation query failed (HTTP {}): {}",
                     result.error().status_code, result.error().message);
      break;
    }

    const auto &page = *result;
    for (const auto &alpaca_order : page) {
      if (alpaca_order.id.empty() || isCompleted(alpaca_order.id)) {
        continue;
      }

      const auto side_opt = parseSide(alpaca_order.side);
      if (!side_opt.has_value()) {
        continue;
      }

      if (alpaca_order.status == "filled" ||
          alpaca_order.status == "partially_filled") {
        reconciled_fills += reconcileFill(alpaca_order, *side_opt);
      } else if (alpaca_order.status == "canceled" ||
                 alpaca_order.status == "expired" ||
                 alpaca_order.status == "rejected") {
        reconciled_cancels += reconcileCancel(alpaca_order, *side_opt);
      }
    }

    if (static_cast<int64_t>(page.size()) < kPageSize || page.empty()) {
      break;
    }
    cursor = page.back().created_at;
    if (cursor.empty()) {
      break;
    }
  }

  logger_->info("Reconciliation complete (fills={} cancels={})",
                reconciled_fills, reconciled_cancels);
}

int64_t AlpacaFillListener::reconcileFill(const AlpacaOrderStatus &alpaca_order,
                                          OrderSide side) {
  const int64_t already_filled = findFilledQty(alpaca_order.id);
  const int64_t remaining = alpaca_order.filled_qty - already_filled;

  if (remaining <= 0) {
    return 0;
  }

  Fill fill{};
  copySymbol(fill.symbol, alpaca_order.symbol);
  fill.executionId = 0;
  fill.orderId = 0;
  fill.side = side;
  fill.quantity = remaining;
  fill.price = alpaca_order.filled_avg_price;
  position_manager_->onFill(fill);

  upsertFilledQty(alpaca_order.id, alpaca_order.filled_qty);
  if (alpaca_order.status == "filled") {
    markCompleted(alpaca_order.id);
    if (pending_tracker_) {
      SymbolKey key{};
      copySymbol(key.value, alpaca_order.symbol);
      pending_tracker_->onOrderCompleted(key, side, alpaca_order.id);
    }
  }

  logger_->info("Reconciled fill ({} qty={} avg_price={})", alpaca_order.symbol,
                remaining, alpaca_order.filled_avg_price);
  return 1;
}

int64_t
AlpacaFillListener::reconcileCancel(const AlpacaOrderStatus &alpaca_order,
                                    OrderSide side) {
  const int64_t already_filled = findFilledQty(alpaca_order.id);
  const int64_t missed_fills = alpaca_order.filled_qty - already_filled;

  if (missed_fills > 0) {
    Fill fill{};
    copySymbol(fill.symbol, alpaca_order.symbol);
    fill.executionId = 0;
    fill.orderId = 0;
    fill.side = side;
    fill.quantity = missed_fills;
    fill.price = alpaca_order.filled_avg_price;
    position_manager_->onFill(fill);
    upsertFilledQty(alpaca_order.id, alpaca_order.filled_qty);
  }

  const int64_t unfilled =
      (std::max)(int64_t{0}, alpaca_order.qty - alpaca_order.filled_qty);
  if (unfilled > 0) {
    Order order{};
    order.id = 0;
    copySymbol(order.symbol, alpaca_order.symbol);
    order.quantity = unfilled;
    order.price = 0;
    order.side = side;
    order.type = OrderType::Market;
    position_manager_->onOrderCancelled(order);
  }

  markCompleted(alpaca_order.id);
  if (pending_tracker_) {
    SymbolKey key{};
    copySymbol(key.value, alpaca_order.symbol);
    pending_tracker_->onOrderCompleted(key, side, alpaca_order.id);
  }

  logger_->info("Reconciled cancel ({} filled={} unfilled={})",
                alpaca_order.symbol, missed_fills, unfilled);
  return 1;
}

int64_t
AlpacaFillListener::findFilledQty(std::string_view order_id) const noexcept {
  for (const auto &e : filled_entries_) {
    if (std::string_view(e.order_id, strnlen(e.order_id, kOrderIdCapacity)) ==
        order_id) {
      return e.filled_qty;
    }
  }
  return 0;
}

void AlpacaFillListener::upsertFilledQty(std::string_view order_id,
                                         int64_t qty) {
  for (auto &e : filled_entries_) {
    if (std::string_view(e.order_id, strnlen(e.order_id, kOrderIdCapacity)) ==
        order_id) {
      e.filled_qty = qty;
      return;
    }
  }
  if (filled_entries_.size() >= kMaxTrackedOrders) {
    filled_entries_.erase(filled_entries_.begin());
  }
  FilledEntry entry{};
  copyOrderId(entry.order_id, std::string(order_id));
  entry.filled_qty = qty;
  filled_entries_.push_back(entry);
}

bool AlpacaFillListener::isCompleted(std::string_view order_id) const noexcept {
  for (const auto &e : completed_entries_) {
    if (std::string_view(e.order_id, strnlen(e.order_id, kOrderIdCapacity)) ==
        order_id) {
      return true;
    }
  }
  return false;
}

void AlpacaFillListener::markCompleted(std::string_view order_id) {
  if (isCompleted(order_id)) {
    return;
  }
  if (completed_entries_.size() >= kMaxTrackedOrders) {
    completed_entries_.erase(completed_entries_.begin());
  }
  CompletedEntry entry{};
  copyOrderId(entry.order_id, std::string(order_id));
  completed_entries_.push_back(entry);
}
