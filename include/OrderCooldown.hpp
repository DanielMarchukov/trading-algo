#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

class alignas(64) OrderCooldown {
public:
  static constexpr uint32_t kMaxSymbols = 64;
  static constexpr uint64_t kDefaultCooldownNs = 100'000'000;

  explicit OrderCooldown(uint64_t cooldown_ns = kDefaultCooldownNs) noexcept;

  void registerSymbol(std::string_view symbol) noexcept;

  [[nodiscard]] bool checkAndUpdate(const char *symbol) noexcept;

  [[nodiscard]] uint64_t cooldownNs() const noexcept;

private:
  struct alignas(64) CooldownSlot {
    char symbol[8];
    std::atomic<uint64_t> last_order_time{0};
  };

  static_assert(sizeof(CooldownSlot) == 64,
                "CooldownSlot must be exactly one cache line");
  static_assert(alignof(CooldownSlot) == 64,
                "CooldownSlot must be cache-line aligned");

  CooldownSlot slots_[kMaxSymbols];
  uint32_t num_symbols_{0};
  uint64_t cooldown_ns_;
};

static_assert(alignof(OrderCooldown) == 64,
              "OrderCooldown must be cache-line aligned");
static_assert(std::is_trivially_destructible_v<OrderCooldown>,
              "OrderCooldown must be trivially destructible");
