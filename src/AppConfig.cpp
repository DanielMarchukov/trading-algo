#include "AppConfig.hpp"
#include "Utils.hpp"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {

void parseTrading(const nlohmann::json &j, TradingConfig &c) {
  if (!j.contains("trading")) {
    return;
  }
  const auto &t = j["trading"];
  c.symbols = t.value("symbols", c.symbols);
  c.order_cooldown_ns = t.value("order_cooldown_ns", c.order_cooldown_ns);
}

void parseRisk(const nlohmann::json &j, RiskConfig &c) {
  if (!j.contains("risk")) {
    return;
  }
  const auto &r = j["risk"];
  c.max_position_per_symbol =
      r.value("max_position_per_symbol", c.max_position_per_symbol);
  c.max_order_value = r.value("max_order_value", c.max_order_value);
}

void parseStrategy(const nlohmann::json &j, StrategyConfig &c) {
  if (!j.contains("strategy")) {
    return;
  }
  const auto &s = j["strategy"];
  c.spread_offset_ticks = s.value("spread_offset_ticks", c.spread_offset_ticks);
  c.order_quantity = s.value("order_quantity", c.order_quantity);
}

void parseAlpaca(const nlohmann::json &j, AlpacaConfig &c) {
  if (!j.contains("alpaca")) {
    return;
  }
  const auto &a = j["alpaca"];
  c.base_url = a.value("base_url", c.base_url);
  c.market_data_url = a.value("market_data_url", c.market_data_url);
  c.fill_stream_url = a.value("fill_stream_url", c.fill_stream_url);
  c.rate_limit_threshold =
      a.value("rate_limit_threshold", c.rate_limit_threshold);
}

void parseZmq(const nlohmann::json &j, ZmqConfig &c) {
  if (!j.contains("zmq")) {
    return;
  }
  const auto &z = j["zmq"];
  c.market_data_address = z.value("market_data_address", c.market_data_address);
}

void parseThreading(const nlohmann::json &j, ThreadingConfig &c) {
  if (!j.contains("threading")) {
    return;
  }
  const auto &th = j["threading"];
  c.gateway_core = th.value("gateway_core", c.gateway_core);
  c.market_pipeline_core =
      th.value("market_pipeline_core", c.market_pipeline_core);
  c.consumer_core_start =
      th.value("consumer_core_start", c.consumer_core_start);
}

void validate(const AppConfig &config) {
  if (config.risk.max_position_per_symbol <= 0) {
    throw std::runtime_error(
        "Config: risk.max_position_per_symbol must be positive");
  }
  if (config.risk.max_order_value <= 0.0) {
    throw std::runtime_error("Config: risk.max_order_value must be positive");
  }
  if (config.strategy.order_quantity <= 0) {
    throw std::runtime_error(
        "Config: strategy.order_quantity must be positive");
  }
  if (config.strategy.spread_offset_ticks == 0) {
    throw std::runtime_error(
        "Config: strategy.spread_offset_ticks must be non-zero");
  }
}

} // namespace

AppConfig loadConfig(const std::optional<std::string> &path) {
  AppConfig config;

  std::filesystem::path file_path;
  if (path.has_value()) {
    file_path = *path;
    if (!std::filesystem::exists(file_path)) {
      throw std::runtime_error("Config file not found: " + path.value());
    }
  } else {
    file_path = "config.json";
    if (!std::filesystem::exists(file_path)) {
      config.zmq.market_data_address = getZmqMarketDataAddress();
      return config;
    }
  }

  std::ifstream file(file_path);
  if (!file.is_open()) {
    throw std::runtime_error("Cannot open config file: " + file_path.string());
  }

  nlohmann::json j;
  try {
    j = nlohmann::json::parse(file);
  } catch (const nlohmann::json::exception &e) {
    throw std::runtime_error("Invalid JSON in config file: " +
                             std::string(e.what()));
  }

  parseTrading(j, config.trading);
  parseRisk(j, config.risk);
  parseStrategy(j, config.strategy);
  parseAlpaca(j, config.alpaca);
  parseZmq(j, config.zmq);
  parseThreading(j, config.threading);

  if (config.zmq.market_data_address.empty()) {
    config.zmq.market_data_address = getZmqMarketDataAddress();
  }

  validate(config);

  return config;
}
