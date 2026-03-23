#include "AlpacaRestClient.hpp"
#include "AppConfig.hpp"
#include "Logging.hpp"
#include "TradingEngine.hpp"
#include <memory>
#include <optional>
#include <spdlog/spdlog.h>
#include <string_view>

int main(int argc, char *argv[]) {
  try {
    logging::init();

    std::optional<std::string> config_path;
    for (int i = 1; i < argc; ++i) {
      if (std::string_view(argv[i]) == "--config" && i + 1 < argc) {
        config_path = argv[++i];
      }
    }

    const auto config = loadConfig(config_path);

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
