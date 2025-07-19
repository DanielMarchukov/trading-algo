#include "AlpacaRestClient.hpp"
#include "TradingEngine.hpp"
#include <iostream>
#include <memory>
#include <vector>

int main() {
    try {
        const std::vector<std::string> symbols = {"AAPL", "GOOGL", "AMZN"};

        auto alpaca_client = std::make_unique<AlpacaRestClient>();

        TradingEngine engine(symbols, std::move(alpaca_client));
        engine.run();
    } catch (const std::exception &e) {
        std::cerr << "An exception occurred: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "An unknown exception occurred." << std::endl;
        return 1;
    }
    return 0;
}
