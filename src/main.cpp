#include "AlpacaRestClient.hpp"
#include "AppConfig.hpp"
#include "Logging.hpp"
#include "TradingEngine.hpp"
#include <memory>
#include <optional>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string_view>

int main(int argc, char *argv[]) {
  try {
    logging::init();

    std::optional<std::string> config_path;
    std::optional<std::string> symbols_override;
    for (int i = 1; i < argc; ++i) {
      const std::string_view arg(argv[i]);
      if (arg == "--config") {
        if (i + 1 >= argc) {
          throw std::runtime_error("--config requires a file path argument");
        }
        config_path = argv[++i];
      } else if (arg == "--symbols") {
        if (i + 1 >= argc) {
          throw std::runtime_error("--symbols requires a comma-separated list");
        }
        symbols_override = argv[++i];
      } else if (arg.starts_with("--")) {
        throw std::runtime_error("Unknown option: " + std::string(arg));
      }
    }

    auto config = loadConfig(config_path);

    if (symbols_override) {
      config.trading.symbols.clear();
      std::istringstream stream(*symbols_override);
      std::string symbol;
      while (std::getline(stream, symbol, ',')) {
        if (!symbol.empty()) {
          if (symbol.size() > 8) {
            throw std::runtime_error("Symbol '" + symbol +
                                     "' exceeds 8-character limit");
          }
          config.trading.symbols.push_back(symbol);
        }
      }
      if (config.trading.symbols.empty()) {
        throw std::runtime_error("--symbols must specify at least one symbol");
      }
    }

    validateConfig(config);

    spdlog::info("Trading symbols: [{}]", [&config]() {
      std::string s;
      for (size_t i = 0; i < config.trading.symbols.size(); ++i) {
        if (i > 0) {
          s += ", ";
        }
        s += config.trading.symbols[i];
      }
      return s;
    }());

    auto alpaca_client = std::make_unique<AlpacaRestClient>(
        config.alpaca.base_url, config.alpaca.rate_limit_threshold);
    auto reconciliation_client = std::make_unique<AlpacaRestClient>(
        config.alpaca.base_url, config.alpaca.rate_limit_threshold);

    TradingEngine engine(config, std::move(alpaca_client),
                         std::move(reconciliation_client));
    engine.run();
  } catch (const std::exception &e) {
    spdlog::error("An exception occurred: {}", e.what());
    logging::shutdown();
    return 1;
  } catch (...) {
    spdlog::error("An unknown exception occurred.");
    logging::shutdown();
    return 1;
  }
  logging::shutdown();
  return 0;
}
