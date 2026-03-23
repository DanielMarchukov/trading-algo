#include "AlpacaWebSocketSource.hpp"
#include "ThreadPinning.hpp"
#include <msgpack.hpp>
#include <spdlog/spdlog.h>

namespace {
constexpr int kMinReconnectMs = 100;
constexpr int kMaxReconnectMs = 30000;
} // namespace

AlpacaWebSocketSource::AlpacaWebSocketSource(std::string api_key,
                                             std::string api_secret,
                                             std::vector<std::string> symbols,
                                             std::atomic<bool> &is_running,
                                             std::string data_url)
    : api_key_(std::move(api_key)), api_secret_(std::move(api_secret)),
      symbols_(std::move(symbols)), is_running_(&is_running),
      ws_(std::make_unique<ix::WebSocket>()), data_url_(std::move(data_url)),
      logger_(spdlog::get("market").get()) {}

AlpacaWebSocketSource::~AlpacaWebSocketSource() { stop(); }

void AlpacaWebSocketSource::start() {
  if (thread_.joinable()) {
    return;
  }

  ws_->setUrl(data_url_ + "?encoding=msgpack");
  ws_->setExtraHeaders(
      ix::WebSocketHttpHeaders{{"Content-Type", "application/msgpack"}});
  ws_->setMinWaitBetweenReconnectionRetries(kMinReconnectMs);
  ws_->setMaxWaitBetweenReconnectionRetries(kMaxReconnectMs);

  ws_->setOnMessageCallback(
      [this](const ix::WebSocketMessagePtr &msg) { onMessage(msg); });

  thread_ = std::thread(&ix::WebSocket::run, ws_.get());
  if (pin_thread_to_core(thread_, 1)) {
    logger_->info("Pinned to CPU core 1");
  } else {
    logger_->warn("Failed to pin to CPU core 1");
  }
}

void AlpacaWebSocketSource::stop() {
  if (ws_) {
    ws_->stop();
  }
  if (thread_.joinable()) {
    thread_.join();
  }
}

void AlpacaWebSocketSource::onMessage(const ix::WebSocketMessagePtr &msg) {
  if (!is_running_->load()) {
    return;
  }

  switch (msg->type) {
  case ix::WebSocketMessageType::Open:
    logger_->info("Connected");
    sendAuth();
    break;

  case ix::WebSocketMessageType::Message:
    if (on_data_fn_) {
      on_data_fn_(on_data_ctx_,
                  std::span<const char>(msg->str.data(), msg->str.size()));
    }
    break;

  case ix::WebSocketMessageType::Error:
    logger_->error("WebSocket error: {}", msg->errorInfo.reason);
    break;

  case ix::WebSocketMessageType::Close:
    logger_->info("Closed (code={})", msg->closeInfo.code);
    break;

  default:
    break;
  }
}

void AlpacaWebSocketSource::sendAuth() {
  msgpack::sbuffer buffer;
  msgpack::packer<msgpack::sbuffer> pk(&buffer);
  pk.pack_map(3);
  pk.pack("action");
  pk.pack("auth");
  pk.pack("key");
  pk.pack(api_key_);
  pk.pack("secret");
  pk.pack(api_secret_);
  ws_->sendBinary(std::string(buffer.data(), buffer.size()));
}

void AlpacaWebSocketSource::sendSubscribe() {
  msgpack::sbuffer buffer;
  msgpack::packer<msgpack::sbuffer> pk(&buffer);
  pk.pack_map(3);
  pk.pack("action");
  pk.pack("subscribe");
  pk.pack("quotes");
  pk.pack(symbols_);
  pk.pack("trades");
  pk.pack(symbols_);
  ws_->sendBinary(std::string(buffer.data(), buffer.size()));
  logger_->info("Subscribed to {} symbols", symbols_.size());
}
