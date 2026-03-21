#include "AlpacaRestClient.hpp"
#include "Logging.hpp"
#include "TradingEngine.hpp"
#include <memory>
#include <spdlog/spdlog.h>
#include <vector>

int main() {
  logging::init();
  try {
    const std::vector<std::string> symbols = {"AAPL", "GOOGL", "AMZN"};

    auto alpaca_client = std::make_unique<AlpacaRestClient>();
    auto reconciliation_client = std::make_unique<AlpacaRestClient>();

    TradingEngine engine(symbols, std::move(alpaca_client),
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
