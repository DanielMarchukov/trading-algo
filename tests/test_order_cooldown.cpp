#include "OrderCooldown.hpp"
#include "Utils.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <vector>

class OrderCooldownTest : public ::testing::Test {
protected:
  static constexpr uint64_t kCooldownNs = 50'000'000;

  void SetUp() override {
    cooldown_ = std::make_unique<OrderCooldown>(kCooldownNs);
    cooldown_->registerSymbol("AAPL");
    cooldown_->registerSymbol("GOOGL");
  }

  std::unique_ptr<OrderCooldown> cooldown_;
};

TEST_F(OrderCooldownTest, AllowsFirstOrder) {
  char symbol[8] = {};
  std::memcpy(symbol, "AAPL", 4);
  EXPECT_TRUE(cooldown_->checkAndUpdate(symbol));
}

TEST_F(OrderCooldownTest, RejectsOrderWithinCooldown) {
  char symbol[8] = {};
  std::memcpy(symbol, "AAPL", 4);
  EXPECT_TRUE(cooldown_->checkAndUpdate(symbol));
  EXPECT_FALSE(cooldown_->checkAndUpdate(symbol));
}

TEST_F(OrderCooldownTest, AllowsOrderAfterCooldownExpires) {
  char symbol[8] = {};
  std::memcpy(symbol, "AAPL", 4);
  EXPECT_TRUE(cooldown_->checkAndUpdate(symbol));

  std::this_thread::sleep_for(std::chrono::milliseconds(60));

  EXPECT_TRUE(cooldown_->checkAndUpdate(symbol));
}

TEST_F(OrderCooldownTest, IndependentPerSymbol) {
  char aapl[8] = {};
  char googl[8] = {};
  std::memcpy(aapl, "AAPL", 4);
  std::memcpy(googl, "GOOGL", 5);

  EXPECT_TRUE(cooldown_->checkAndUpdate(aapl));
  EXPECT_TRUE(cooldown_->checkAndUpdate(googl));
}

TEST_F(OrderCooldownTest, RejectsUnregisteredSymbol) {
  char unknown[8] = {};
  std::memcpy(unknown, "TSLA", 4);
  EXPECT_FALSE(cooldown_->checkAndUpdate(unknown));
}

TEST_F(OrderCooldownTest, ZeroCooldownAllowsAll) {
  OrderCooldown zero_cd(0);
  zero_cd.registerSymbol("AAPL");

  char symbol[8] = {};
  std::memcpy(symbol, "AAPL", 4);

  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(zero_cd.checkAndUpdate(symbol));
  }
}

TEST_F(OrderCooldownTest, ConcurrentDifferentSymbols) {
  constexpr int kNumThreads = 4;
  constexpr int kOrdersPerThread = 1000;

  OrderCooldown cd(0);
  const char *symbols[] = {"SYM0", "SYM1", "SYM2", "SYM3"};
  for (const auto *sym : symbols) {
    cd.registerSymbol(sym);
  }

  std::atomic<bool> start{false};
  std::atomic<int> total_approved{0};
  std::vector<std::thread> threads;

  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&, t]() {
      char sym[8] = {};
      std::memcpy(sym, symbols[t], std::strlen(symbols[t]));

      while (!start.load(std::memory_order_acquire)) {
      }

      int approved = 0;
      for (int i = 0; i < kOrdersPerThread; ++i) {
        if (cd.checkAndUpdate(sym)) {
          ++approved;
        }
      }
      total_approved.fetch_add(approved, std::memory_order_relaxed);
    });
  }

  start.store(true, std::memory_order_release);

  for (auto &t : threads) {
    t.join();
  }

  EXPECT_EQ(total_approved.load(), kNumThreads * kOrdersPerThread);
}

TEST_F(OrderCooldownTest, DefaultCooldownValue) {
  OrderCooldown cd;
  EXPECT_EQ(cd.cooldownNs(), OrderCooldown::kDefaultCooldownNs);
}

TEST_F(OrderCooldownTest, CustomCooldownValue) {
  EXPECT_EQ(cooldown_->cooldownNs(), kCooldownNs);
}

TEST_F(OrderCooldownTest, IgnoresRegistrationBeyondMaxSymbols) {
  OrderCooldown cd(0);
  for (uint32_t i = 0; i < OrderCooldown::kMaxSymbols; ++i) {
    char name[8] = {};
    std::snprintf(name, sizeof(name), "S%02u", i);
    cd.registerSymbol(name);
  }

  cd.registerSymbol("OVER");

  char last[8] = {};
  std::snprintf(last, sizeof(last), "S%02u", OrderCooldown::kMaxSymbols - 1);
  EXPECT_TRUE(cd.checkAndUpdate(last));

  char over[8] = {};
  std::memcpy(over, "OVER", 4);
  EXPECT_FALSE(cd.checkAndUpdate(over));
}

TEST_F(OrderCooldownTest, DoesNotUpdateTimestampOnRejection) {
  char symbol[8] = {};
  std::memcpy(symbol, "AAPL", 4);

  EXPECT_TRUE(cooldown_->checkAndUpdate(symbol));

  for (int i = 0; i < 10; ++i) {
    EXPECT_FALSE(cooldown_->checkAndUpdate(symbol));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  EXPECT_TRUE(cooldown_->checkAndUpdate(symbol));
}
