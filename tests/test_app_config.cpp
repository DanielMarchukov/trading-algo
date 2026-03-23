#include "AppConfig.hpp"
#include "Utils.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

class AppConfigTest : public ::testing::Test {
protected:
  void TearDown() override {
    if (!temp_path_.empty()) {
      std::filesystem::remove(temp_path_);
    }
  }

  void writeConfigFile(const std::string &content) {
    temp_path_ = std::filesystem::temp_directory_path() / "test_config.json";
    std::ofstream f(temp_path_);
    f << content;
  }

  std::filesystem::path temp_path_;
};

TEST_F(AppConfigTest, DefaultConfigWithNoFile) {
  const auto config = loadConfig();

  EXPECT_EQ(config.trading.symbols.size(), 3);
  EXPECT_EQ(config.trading.symbols[0], "AAPL");
  EXPECT_EQ(config.trading.order_cooldown_ns, 100'000'000ULL);
  EXPECT_EQ(config.risk.max_position_per_symbol, 1000);
  EXPECT_DOUBLE_EQ(config.risk.max_order_value, 10000.00);
  EXPECT_EQ(config.strategy.spread_offset_ticks, 100ULL);
  EXPECT_EQ(config.strategy.order_quantity, 100);
  EXPECT_EQ(config.alpaca.base_url, "https://paper-api.alpaca.markets");
  EXPECT_EQ(config.threading.gateway_core, 0U);
  EXPECT_EQ(config.threading.consumer_core_start, 2U);
  EXPECT_EQ(config.zmq.market_data_address, getZmqMarketDataAddress());
}

TEST_F(AppConfigTest, LoadsFromFile) {
  writeConfigFile(R"({
    "trading": {"symbols": ["SPY", "QQQ"], "order_cooldown_ns": 50000000},
    "risk": {"max_position_per_symbol": 500, "max_order_value": 5000.0},
    "strategy": {"spread_offset_ticks": 50, "order_quantity": 25},
    "alpaca": {"base_url": "https://api.alpaca.markets"},
    "threading": {"gateway_core": 1, "consumer_core_start": 4}
  })");

  const auto config = loadConfig(temp_path_.string());

  EXPECT_EQ(config.trading.symbols.size(), 2);
  EXPECT_EQ(config.trading.symbols[0], "SPY");
  EXPECT_EQ(config.trading.symbols[1], "QQQ");
  EXPECT_EQ(config.trading.order_cooldown_ns, 50'000'000ULL);
  EXPECT_EQ(config.risk.max_position_per_symbol, 500);
  EXPECT_DOUBLE_EQ(config.risk.max_order_value, 5000.0);
  EXPECT_EQ(config.strategy.spread_offset_ticks, 50ULL);
  EXPECT_EQ(config.strategy.order_quantity, 25);
  EXPECT_EQ(config.alpaca.base_url, "https://api.alpaca.markets");
  EXPECT_EQ(config.threading.gateway_core, 1U);
  EXPECT_EQ(config.threading.consumer_core_start, 4U);
}

TEST_F(AppConfigTest, MissingFieldsGetDefaults) {
  writeConfigFile(R"({"risk": {"max_position_per_symbol": 500}})");

  const auto config = loadConfig(temp_path_.string());

  EXPECT_EQ(config.risk.max_position_per_symbol, 500);
  EXPECT_DOUBLE_EQ(config.risk.max_order_value, 10000.00);
  EXPECT_EQ(config.trading.symbols.size(), 3);
  EXPECT_EQ(config.strategy.order_quantity, 100);
}

TEST_F(AppConfigTest, InvalidJsonThrows) {
  writeConfigFile("{invalid json");

  EXPECT_THROW((void)loadConfig(temp_path_.string()), std::runtime_error);
}

TEST_F(AppConfigTest, ExplicitPathNotFoundThrows) {
  EXPECT_THROW((void)loadConfig("/nonexistent/path/config.json"),
               std::runtime_error);
}

TEST_F(AppConfigTest, NegativeMaxPositionThrows) {
  writeConfigFile(R"({"risk": {"max_position_per_symbol": -1}})");

  EXPECT_THROW((void)loadConfig(temp_path_.string()), std::runtime_error);
}

TEST_F(AppConfigTest, ZeroOrderQuantityThrows) {
  writeConfigFile(R"({"strategy": {"order_quantity": 0}})");

  EXPECT_THROW((void)loadConfig(temp_path_.string()), std::runtime_error);
}

TEST_F(AppConfigTest, ZmqAddressDefaultIsPlatformSpecific) {
  writeConfigFile(R"({})");

  const auto config = loadConfig(temp_path_.string());

  EXPECT_EQ(config.zmq.market_data_address, getZmqMarketDataAddress());
}

TEST_F(AppConfigTest, EmptySymbolListIsAllowed) {
  writeConfigFile(R"({"trading": {"symbols": []}})");

  const auto config = loadConfig(temp_path_.string());

  EXPECT_TRUE(config.trading.symbols.empty());
}

TEST_F(AppConfigTest, UnknownFieldsIgnored) {
  writeConfigFile(
      R"({"unknown_section": {"foo": 42}, "risk": {"max_position_per_symbol": 500}})");

  const auto config = loadConfig(temp_path_.string());

  EXPECT_EQ(config.risk.max_position_per_symbol, 500);
}

TEST_F(AppConfigTest, DuplicateSymbolsThrows) {
  writeConfigFile(R"({"trading": {"symbols": ["AAPL", "AAPL"]}})");

  EXPECT_THROW((void)loadConfig(temp_path_.string()), std::runtime_error);
}

TEST_F(AppConfigTest, NonObjectRootThrows) {
  writeConfigFile("[1, 2, 3]");

  EXPECT_THROW((void)loadConfig(temp_path_.string()), std::runtime_error);
}
