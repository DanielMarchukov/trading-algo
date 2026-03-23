#pragma once

#include "MarketDataPipeline.hpp"
#include <atomic>
#include <ixwebsocket/IXWebSocket.h>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace spdlog {
class logger;
} // namespace spdlog

class AlpacaWebSocketSource {
public:
  using RawDataFn = void (*)(void *, std::span<const char>);

  AlpacaWebSocketSource(
      std::string api_key, std::string api_secret,
      std::vector<std::string> symbols, std::atomic<bool> &is_running,
      std::string data_url = "wss://stream.data.alpaca.markets/v2/iex");
  ~AlpacaWebSocketSource();

  AlpacaWebSocketSource(AlpacaWebSocketSource &&) = default;
  AlpacaWebSocketSource &operator=(AlpacaWebSocketSource &&) = delete;
  AlpacaWebSocketSource(const AlpacaWebSocketSource &) = delete;
  AlpacaWebSocketSource &operator=(const AlpacaWebSocketSource &) = delete;

  void setOnData(RawDataFn fn, void *ctx) {
    on_data_fn_ = fn;
    on_data_ctx_ = ctx;
  }

  void start();
  void stop();
  void sendSubscribe();

protected:
  void onMessage(const ix::WebSocketMessagePtr &msg);

private:
  void sendAuth();

  std::string api_key_;
  std::string api_secret_;
  std::vector<std::string> symbols_;
  std::atomic<bool> *is_running_;

  std::unique_ptr<ix::WebSocket> ws_;
  std::thread thread_;
  RawDataFn on_data_fn_ = nullptr;
  void *on_data_ctx_ = nullptr;
  std::string data_url_;
  spdlog::logger *logger_;
};

static_assert(MarketDataSourceLike<AlpacaWebSocketSource>,
              "AlpacaWebSocketSource must satisfy MarketDataSourceLike");
