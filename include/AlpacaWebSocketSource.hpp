#pragma once

#include "MarketDataPipeline.hpp"
#include <atomic>
#include <ixwebsocket/IXWebSocket.h>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

class AlpacaWebSocketSource {
public:
  using RawDataFn = void (*)(void *, std::span<const char>);

  AlpacaWebSocketSource(std::string api_key, std::string api_secret,
                        std::vector<std::string> symbols,
                        std::atomic<bool> &is_running);
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

private:
  void onMessage(const ix::WebSocketMessagePtr &msg);
  void sendAuth();

  std::string api_key_;
  std::string api_secret_;
  std::vector<std::string> symbols_;
  std::atomic<bool> *is_running_;

  std::unique_ptr<ix::WebSocket> ws_;
  std::thread thread_;
  RawDataFn on_data_fn_ = nullptr;
  void *on_data_ctx_ = nullptr;
};

static_assert(MarketDataSourceLike<AlpacaWebSocketSource>,
              "AlpacaWebSocketSource must satisfy MarketDataSourceLike");
