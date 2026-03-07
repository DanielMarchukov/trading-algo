#include "IRestClient.hpp"
#include "TradingEngine.hpp"
#include "Utils.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

class MockRestClientForEngine : public IRestClient {
public:
  void placeOrder(const Order & /*order*/) override {
    order_count_.fetch_add(1, std::memory_order_relaxed);
  }

  int getOrderCount() const {
    return order_count_.load(std::memory_order_relaxed);
  }

private:
  std::atomic<int> order_count_{0};
};

class TradingEngineTest : public ::testing::Test {
protected:
  void SetUp() override {
    setenv("APCA_API_KEY_ID", "test_key", 1);
    setenv("APCA_API_SECRET_KEY", "test_secret", 1);
  }

  void TearDown() override {
    // Ensure environment is restored
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

TEST_F(TradingEngineTest, ThrowsWhenApiCredentialsMissing) {
  unsetenv("APCA_API_KEY_ID");
  unsetenv("APCA_API_SECRET_KEY");

  std::vector<std::string> symbols = {"AAPL"};
  auto mock_client = std::make_unique<MockRestClientForEngine>();

  EXPECT_THROW(TradingEngine(symbols, std::move(mock_client)),
               std::runtime_error);
}

TEST_F(TradingEngineTest, ThrowsWhenApiKeyMissing) {
  unsetenv("APCA_API_KEY_ID");
  setenv("APCA_API_SECRET_KEY", "test_secret", 1);

  std::vector<std::string> symbols = {"AAPL"};
  auto mock_client = std::make_unique<MockRestClientForEngine>();

  EXPECT_THROW(TradingEngine(symbols, std::move(mock_client)),
               std::runtime_error);
}

TEST_F(TradingEngineTest, ThrowsWhenApiSecretMissing) {
  setenv("APCA_API_KEY_ID", "test_key", 1);
  unsetenv("APCA_API_SECRET_KEY");

  std::vector<std::string> symbols = {"AAPL"};
  auto mock_client = std::make_unique<MockRestClientForEngine>();

  EXPECT_THROW(TradingEngine(symbols, std::move(mock_client)),
               std::runtime_error);
}

TEST_F(TradingEngineTest, RunAndStopCompletesCleanly) {
  std::vector<std::string> symbols = {"AAPL"};
  auto mock_client = std::make_unique<MockRestClientForEngine>();
  TradingEngine engine(symbols, std::move(mock_client));

  std::thread engine_thread([&engine]() { engine.run(); });

  // Let the engine run briefly
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Stop the engine
  engine.stop();

  // Wait for clean shutdown
  engine_thread.join();
}

TEST_F(TradingEngineTest, StopFromSeparateThreadWorks) {
  std::vector<std::string> symbols = {"AAPL"};
  auto mock_client = std::make_unique<MockRestClientForEngine>();
  TradingEngine engine(symbols, std::move(mock_client));

  std::thread engine_thread([&engine]() { engine.run(); });

  std::thread stopper([&engine]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.stop();
  });

  engine_thread.join();
  stopper.join();
}

TEST_F(TradingEngineTest, MultipleSymbolsInitializeCorrectly) {
  std::vector<std::string> symbols = {"AAPL", "GOOGL", "MSFT", "TSLA"};
  auto mock_client = std::make_unique<MockRestClientForEngine>();

  EXPECT_NO_THROW(TradingEngine(symbols, std::move(mock_client)));
}

TEST_F(TradingEngineTest, DestructorCleansUpWithoutRun) {
  std::vector<std::string> symbols = {"AAPL"};
  auto mock_client = std::make_unique<MockRestClientForEngine>();

  {
    TradingEngine engine(symbols, std::move(mock_client));
    // Destructor should clean up safely
  }
}

TEST_F(TradingEngineTest, DestructorCleansUpAfterRun) {
  std::vector<std::string> symbols = {"AAPL"};
  auto mock_client = std::make_unique<MockRestClientForEngine>();

  std::thread engine_thread;
  {
    TradingEngine engine(symbols, std::move(mock_client));
    engine_thread = std::thread([&engine]() { engine.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop();
  }

  if (engine_thread.joinable()) {
    engine_thread.join();
  }
}

TEST_F(TradingEngineTest, HandlesRapidStartStop) {
  std::vector<std::string> symbols = {"AAPL"};
  auto mock_client = std::make_unique<MockRestClientForEngine>();
  TradingEngine engine(symbols, std::move(mock_client));

  std::thread engine_thread([&engine]() { engine.run(); });

  // Immediately stop
  engine.stop();

  engine_thread.join();
}

TEST_F(TradingEngineTest, ConstructsWithSingleSymbol) {
  std::vector<std::string> symbols = {"AAPL"};
  auto mock_client = std::make_unique<MockRestClientForEngine>();

  EXPECT_NO_THROW(TradingEngine(symbols, std::move(mock_client)));
}

TEST_F(TradingEngineTest, ConstructsWithLongSymbolList) {
  std::vector<std::string> symbols;
  for (int i = 0; i < 10; ++i) {
    symbols.push_back("SYM" + std::to_string(i));
  }
  auto mock_client = std::make_unique<MockRestClientForEngine>();

  EXPECT_NO_THROW(TradingEngine(symbols, std::move(mock_client)));
}