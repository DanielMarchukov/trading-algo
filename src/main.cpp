#include "AlpacaRestClient.hpp"
#include "AppConfig.hpp"
#include "Logging.hpp"
#include "TradingEngine.hpp"
#include <memory>
#include <optional>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string_view>

namespace {

struct CliArgs {
  std::optional<std::string> config_path;
  std::optional<std::string> symbols;
};

[[nodiscard]] CliArgs parseArgs(int argc, char *argv[]) {
  CliArgs args;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--config") {
      if (i + 1 >= argc) {
        throw std::runtime_error("--config requires a file path argument");
      }
      args.config_path = argv[++i];
    } else if (arg == "--symbols") {
      if (i + 1 >= argc) {
        throw std::runtime_error("--symbols requires a comma-separated list");
      }
      args.symbols = argv[++i];
    } else if (arg.starts_with("--")) {
      throw std::runtime_error("Unknown option: " + std::string(arg));
    }
  }
  return args;
}

void applySymbolsOverride(AppConfig &config, const std::string &csv) {
  config.trading.symbols.clear();
  std::istringstream stream(csv);
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

void logSymbols(const AppConfig &config) {
  std::string joined;
  for (size_t i = 0; i < config.trading.symbols.size(); ++i) {
    if (i > 0) {
      joined += ", ";
    }
    joined += config.trading.symbols[i];
  }
  spdlog::info("Trading symbols: [{}]", joined);
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    logging::init();

    const auto args = parseArgs(argc, argv);
    auto config = loadConfig(args.config_path);
    if (args.symbols) {
      applySymbolsOverride(config, *args.symbols);
    }
    validateConfig(config);
    logSymbols(config);

    auto rest = std::make_unique<AlpacaRestClient>(
        config.alpaca.base_url, config.alpaca.rate_limit_threshold);
    auto reconciliation = std::make_unique<AlpacaRestClient>(
        config.alpaca.base_url, config.alpaca.rate_limit_threshold);

    TradingEngine engine(config, std::move(rest), std::move(reconciliation));
    engine.run();
  } catch (const std::exception &e) {
    spdlog::error("{}", e.what());
    logging::shutdown();
    return 1;
  } catch (...) {
    spdlog::error("Unknown fatal error");
    logging::shutdown();
    return 1;
  }
  logging::shutdown();
  return 0;
}
