#include "AlpacaWebSocketSource.hpp"
#include "ThreadPinning.hpp"
#include <iostream>
#include <msgpack.hpp>

namespace {
constexpr const char *kDataStreamUrl =
    "wss://stream.data.alpaca.markets/v2/iex";
constexpr int kMinReconnectMs = 100;
constexpr int kMaxReconnectMs = 30000;
} // namespace

AlpacaWebSocketSource::AlpacaWebSocketSource(std::string api_key,
                                             std::string api_secret,
                                             std::vector<std::string> symbols,
                                             std::atomic<bool> &is_running)
    : api_key_(std::move(api_key)), api_secret_(std::move(api_secret)),
      symbols_(std::move(symbols)), is_running_(&is_running),
      ws_(std::make_unique<ix::WebSocket>()) {}

AlpacaWebSocketSource::~AlpacaWebSocketSource() { stop(); }

void AlpacaWebSocketSource::start() {
  if (thread_.joinable()) {
    return;
  }

  ws_->setUrl(std::string(kDataStreamUrl) + "?encoding=msgpack");
  ws_->setExtraHeaders(
      ix::WebSocketHttpHeaders{{"Content-Type", "application/msgpack"}});
  ws_->setMinWaitBetweenReconnectionRetries(kMinReconnectMs);
  ws_->setMaxWaitBetweenReconnectionRetries(kMaxReconnectMs);

  ws_->setOnMessageCallback(
      [this](const ix::WebSocketMessagePtr &msg) { onMessage(msg); });

  thread_ = std::thread(&ix::WebSocket::run, ws_.get());
  pin_thread_to_core(thread_, 1);
  std::cout << "AlpacaWebSocketSource: pinned to CPU core 1" << '\n';
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
    std::cout << "AlpacaWebSocketSource: connected" << '\n';
    sendAuth();
    break;

  case ix::WebSocketMessageType::Message:
    if (on_data_fn_) {
      on_data_fn_(on_data_ctx_,
                  std::span<const char>(msg->str.data(), msg->str.size()));
    }
    break;

  case ix::WebSocketMessageType::Error:
    std::cerr << "AlpacaWebSocketSource: error: " << msg->errorInfo.reason
              << '\n';
    break;

  case ix::WebSocketMessageType::Close:
    std::cout << "AlpacaWebSocketSource: closed (code=" << msg->closeInfo.code
              << ")" << '\n';
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
  std::cout << "AlpacaWebSocketSource: subscribed to " << symbols_.size()
            << " symbols" << '\n';
}
