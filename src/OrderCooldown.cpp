#include "OrderCooldown.hpp"
#include "Utils.hpp"
#include <algorithm>
#include <cstring>

OrderCooldown::OrderCooldown(const uint64_t cooldown_ns) noexcept
    : slots_{}, cooldown_ns_(cooldown_ns) {}

void OrderCooldown::registerSymbol(const std::string_view symbol) noexcept {
  if (num_symbols_ >= kMaxSymbols) {
    return;
  }
  auto &slot = slots_[num_symbols_];
  std::memset(slot.symbol, 0, sizeof(slot.symbol));
  const auto len =
      (std::min)(symbol.size(), static_cast<size_t>(sizeof(slot.symbol)));
  std::memcpy(slot.symbol, symbol.data(), len);
  slot.last_order_time.store(0, std::memory_order_relaxed);
  ++num_symbols_;
}

bool OrderCooldown::checkAndUpdate(const char *symbol) noexcept {
  for (uint32_t i = 0; i < num_symbols_; ++i) {
    if (std::memcmp(slots_[i].symbol, symbol, sizeof(slots_[i].symbol)) == 0) {
      const uint64_t now = nowNanos();
      const uint64_t last =
          slots_[i].last_order_time.load(std::memory_order_relaxed);
      if (now - last < cooldown_ns_) [[unlikely]] {
        return false;
      }
      slots_[i].last_order_time.store(now, std::memory_order_relaxed);
      return true;
    }
  }
  return false;
}

uint64_t OrderCooldown::cooldownNs() const noexcept { return cooldown_ns_; }
