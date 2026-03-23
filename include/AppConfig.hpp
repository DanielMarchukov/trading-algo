#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct TradingConfig {
  std::vector<std::string> symbols = {"AAPL", "GOOGL", "AMZN"};
  uint64_t order_cooldown_ns = 100'000'000;
};

struct RiskConfig {
  int64_t max_position_per_symbol = 1000;
  double max_order_value = 10000.00;
};

struct StrategyConfig {
  uint64_t spread_offset_ticks = 100;
  int64_t order_quantity = 100;
};

struct AlpacaConfig {
  std::string base_url = "https://paper-api.alpaca.markets";
  std::string market_data_url = "wss://stream.data.alpaca.markets/v2/iex";
  std::string fill_stream_url = "wss://paper-api.alpaca.markets/stream";
  int64_t rate_limit_threshold = 10;
};

struct ZmqConfig {
  std::string market_data_address;
};

struct ThreadingConfig {
  uint32_t gateway_core = 0;
  uint32_t market_pipeline_core = 1;
  uint32_t consumer_core_start = 2;
};

struct AppConfig {
  TradingConfig trading;
  RiskConfig risk;
  StrategyConfig strategy;
  AlpacaConfig alpaca;
  ZmqConfig zmq;
  ThreadingConfig threading;
};

[[nodiscard]] AppConfig
loadConfig(const std::optional<std::string> &path = std::nullopt);
