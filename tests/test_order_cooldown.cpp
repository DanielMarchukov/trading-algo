#include "OrderCooldown.hpp"
#include "ThreadGuard.hpp"
#include "Utils.hpp"
#include <cstring>
#include <future>
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
  std::strncpy(symbol, "AAPL", sizeof(symbol));
  EXPECT_TRUE(cooldown_->checkAndUpdate(symbol));
}

TEST_F(OrderCooldownTest, RejectsOrderWithinCooldown) {
  char symbol[8] = {};
  std::strncpy(symbol, "AAPL", sizeof(symbol));
  EXPECT_TRUE(cooldown_->checkAndUpdate(symbol));
  EXPECT_FALSE(cooldown_->checkAndUpdate(symbol));
}

TEST_F(OrderCooldownTest, AllowsOrderAfterCooldownExpires) {
  char symbol[8] = {};
  std::strncpy(symbol, "AAPL", sizeof(symbol));
  EXPECT_TRUE(cooldown_->checkAndUpdate(symbol));

  std::this_thread::sleep_for(std::chrono::milliseconds(60));

  EXPECT_TRUE(cooldown_->checkAndUpdate(symbol));
}

TEST_F(OrderCooldownTest, IndependentPerSymbol) {
  char aapl[8] = {};
  char googl[8] = {};
  std::strncpy(aapl, "AAPL", sizeof(aapl));
  std::strncpy(googl, "GOOGL", sizeof(googl));

  EXPECT_TRUE(cooldown_->checkAndUpdate(aapl));
  EXPECT_TRUE(cooldown_->checkAndUpdate(googl));
}

TEST_F(OrderCooldownTest, RejectsUnregisteredSymbol) {
  char unknown[8] = {};
  std::strncpy(unknown, "TSLA", sizeof(unknown));
  EXPECT_FALSE(cooldown_->checkAndUpdate(unknown));
}

TEST_F(OrderCooldownTest, ZeroCooldownAllowsAll) {
  OrderCooldown zero_cd(0);
  zero_cd.registerSymbol("AAPL");

  char symbol[8] = {};
  std::strncpy(symbol, "AAPL", sizeof(symbol));

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
  std::atomic<int> done_count{0};
  std::promise<void> all_done;
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);

  for (const auto *symbol : symbols) {
    threads.emplace_back([&, symbol]() {
      char sym[8] = {};
      std::strncpy(sym, symbol, sizeof(sym));

      while (!start.load(std::memory_order_acquire)) {
      }

      int approved = 0;
      for (int i = 0; i < kOrdersPerThread; ++i) {
        if (cd.checkAndUpdate(sym)) {
          ++approved;
        }
      }
      total_approved.fetch_add(approved, std::memory_order_relaxed);
      if (done_count.fetch_add(1, std::memory_order_acq_rel) + 1 ==
          kNumThreads) {
        all_done.set_value();
      }
    });
  }

  start.store(true, std::memory_order_release);

  auto status = all_done.get_future().wait_for(std::chrono::seconds(5));
  ASSERT_EQ(status, std::future_status::ready) << "timeout — possible deadlock";

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
    name[0] = 'S';
    name[1] = static_cast<char>('0' + (i / 10));
    name[2] = static_cast<char>('0' + (i % 10));
    cd.registerSymbol(name);
  }

  cd.registerSymbol("OVER");

  constexpr uint32_t kLastIdx = OrderCooldown::kMaxSymbols - 1;
  char last[8] = {};
  last[0] = 'S';
  last[1] = static_cast<char>('0' + (kLastIdx / 10));
  last[2] = static_cast<char>('0' + (kLastIdx % 10));
  EXPECT_TRUE(cd.checkAndUpdate(last));

  char over[8] = {};
  std::strncpy(over, "OVER", sizeof(over));
  EXPECT_FALSE(cd.checkAndUpdate(over));
}

TEST_F(OrderCooldownTest, DoesNotUpdateTimestampOnRejection) {
  char symbol[8] = {};
  std::strncpy(symbol, "AAPL", sizeof(symbol));

  EXPECT_TRUE(cooldown_->checkAndUpdate(symbol));

  for (int i = 0; i < 10; ++i) {
    EXPECT_FALSE(cooldown_->checkAndUpdate(symbol));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  EXPECT_TRUE(cooldown_->checkAndUpdate(symbol));
}
