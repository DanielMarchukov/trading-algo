#include "TradingEngine.hpp"
#include "IRestClient.hpp"
#include <gtest/gtest.h>
#include <memory>

class MockRestClientForEngine : public IRestClient {
public:
    void placeOrder(const Order& /*order*/) override {}
};

class TradingEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        setenv("APCA_API_KEY_ID", "test_key", 1);
        setenv("APCA_API_SECRET_KEY", "test_secret", 1);
    }
};

TEST_F(TradingEngineTest, ConstructsWithValidSymbols) {
    std::vector<std::string> symbols = {"AAPL", "GOOGL"};
    auto mock_client = std::make_unique<MockRestClientForEngine>();

    EXPECT_NO_THROW(TradingEngine(symbols, std::move(mock_client)));
}

TEST_F(TradingEngineTest, ConstructsWithEmptySymbols) {
    std::vector<std::string> symbols = {};
    auto mock_client = std::make_unique<MockRestClientForEngine>();

    EXPECT_NO_THROW(TradingEngine(symbols, std::move(mock_client)));
}

TEST_F(TradingEngineTest, CanStopBeforeRun) {
    std::vector<std::string> symbols = {"AAPL"};
    auto mock_client = std::make_unique<MockRestClientForEngine>();
    TradingEngine engine(symbols, std::move(mock_client));

    EXPECT_NO_THROW(engine.stop());
}

TEST_F(TradingEngineTest, ShutdownIsIdempotent) {
    std::vector<std::string> symbols = {"AAPL"};
    auto mock_client = std::make_unique<MockRestClientForEngine>();
    TradingEngine engine(symbols, std::move(mock_client));

    engine.stop();
    EXPECT_NO_THROW(engine.stop()); // Should handle multiple stops gracefully
}