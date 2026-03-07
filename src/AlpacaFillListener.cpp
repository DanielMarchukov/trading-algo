#include "AlpacaFillListener.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <thread>

void pin_thread_to_core(std::thread &t, size_t core_id);

namespace {

constexpr const char *kStreamUrl = "wss://paper-api.alpaca.markets/stream";
constexpr int kMinReconnectMs = 100;
constexpr int kMaxReconnectMs = 30000;
constexpr std::size_t kSymbolCapacity = 8;

void copySymbol(char (&dest)[8], const std::string &src) {
  std::memset(dest, 0, kSymbolCapacity);
  const auto len = (std::min)(src.size(), kSymbolCapacity);
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
  } catch (const std::exception &) {
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
  } catch (const std::exception &) {
  }
  return std::nullopt;
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
    fill.quantity = *qty;
    fill.price = *price;
    return fill;
  }

  if (event == "canceled" || event == "expired" || event == "rejected") {
    CancelEvent cancel{};
    copySymbol(cancel.symbol, symbol);
    cancel.side = *side;

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
    std::shared_ptr<PositionManager> position_manager,
    std::atomic<bool> &is_running, const std::string &api_key,
    const std::string &api_secret)
    : position_manager_(std::move(position_manager)), is_running_(is_running),
      api_key_(api_key), api_secret_(api_secret) {}

AlpacaFillListener::~AlpacaFillListener() { stop(); }

void AlpacaFillListener::start() {
  ws_.setUrl(kStreamUrl);
  ws_.setMinWaitBetweenReconnectionRetries(kMinReconnectMs);
  ws_.setMaxWaitBetweenReconnectionRetries(kMaxReconnectMs);

  ws_.setOnMessageCallback(
      [this](const ix::WebSocketMessagePtr &msg) { onMessage(msg); });

  thread_ = std::thread(&ix::WebSocket::run, &ws_);
  pin_thread_to_core(thread_, 0);
  std::cout << "Pinned FillListener thread to CPU Core 0" << std::endl;
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
    std::cout << "FillListener: WebSocket connected" << std::endl;
    sendAuth();
    break;

  case ix::WebSocketMessageType::Message:
    handleTradeUpdate(msg->str);
    break;

  case ix::WebSocketMessageType::Error:
    std::cerr << "FillListener: WebSocket error: " << msg->errorInfo.reason
              << std::endl;
    break;

  case ix::WebSocketMessageType::Close:
    std::cout << "FillListener: WebSocket closed (code=" << msg->closeInfo.code
              << ")" << std::endl;
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
  } catch (const nlohmann::json::parse_error &) {
    std::cerr << "FillListener: Failed to parse message" << std::endl;
    return;
  }

  if (parsed.contains("stream") && parsed["stream"] == "authorization") {
    if (parsed.contains("data") && parsed["data"].contains("status") &&
        parsed["data"]["status"] == "authorized") {
      std::cout << "FillListener: Authorized" << std::endl;
      sendSubscribe();
    } else {
      std::cerr << "FillListener: Authorization failed" << std::endl;
    }
    return;
  }

  if (parsed.contains("stream") && parsed["stream"] == "listening") {
    if (parsed.contains("data") && parsed["data"].contains("streams") &&
        parsed["data"]["streams"].is_array()) {
      const auto &streams = parsed["data"]["streams"];
      bool found = false;
      for (const auto &s : streams) {
        if (s.is_string() && s.get<std::string>() == "trade_updates") {
          found = true;
          break;
        }
      }
      if (found) {
        std::cout << "FillListener: Subscribed to trade_updates" << std::endl;
      } else {
        std::cerr << "FillListener: trade_updates not in streams list"
                  << std::endl;
      }
    } else {
      std::cerr << "FillListener: Malformed listening response" << std::endl;
    }
    return;
  }

  const TradeUpdate update = parseTradingUpdate(parsed);

  if (std::holds_alternative<FillEvent>(update)) {
    const auto &fill_event = std::get<FillEvent>(update);
    Fill fill{};
    std::memcpy(fill.symbol, fill_event.symbol, kSymbolCapacity);
    fill.executionId = 0;
    fill.orderId = 0;
    fill.side = fill_event.side;
    fill.quantity = fill_event.quantity;
    fill.price = fill_event.price;
    position_manager_->onFill(fill);
    std::cout << "FillListener: Fill processed ("
              << std::string_view(fill_event.symbol,
                                  strnlen(fill_event.symbol, kSymbolCapacity))
              << " qty=" << fill_event.quantity << ")" << std::endl;
  } else if (std::holds_alternative<CancelEvent>(update)) {
    const auto &cancel_event = std::get<CancelEvent>(update);
    Order order{};
    order.id = 0;
    std::memcpy(order.symbol, cancel_event.symbol, kSymbolCapacity);
    order.quantity = cancel_event.quantity;
    order.price = 0;
    order.side = cancel_event.side;
    order.type = OrderType::Market;
    position_manager_->onOrderCancelled(order);
    std::cout << "FillListener: Cancel processed ("
              << std::string_view(cancel_event.symbol,
                                  strnlen(cancel_event.symbol, kSymbolCapacity))
              << " qty=" << cancel_event.quantity << ")" << std::endl;
  }
}
