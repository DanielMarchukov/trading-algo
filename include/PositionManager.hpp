#pragma once

#include "Fill.hpp"
#include "Order.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string_view>

class PositionManager {
public:
  static constexpr uint32_t kMaxSymbols = 256;

  PositionManager() = default;

  void registerSymbol(std::string_view symbol);
  void onFill(const Fill &fill);
  void onOrderSent(const Order &order) noexcept;
  void onOrderCancelled(const Order &order);

  [[nodiscard]] int64_t getFilledPosition(std::string_view symbol) const;
  [[nodiscard]] int64_t getPendingPosition(std::string_view symbol) const;
  [[nodiscard]] int64_t
  getTotalExposure(std::string_view symbol) const noexcept;

private:
  static constexpr uint64_t kEmptySlot = UINT64_MAX;
  static constexpr uint32_t kMask = kMaxSymbols - 1;

  struct alignas(64) Entry {
    std::atomic<uint64_t> key{kEmptySlot};
    std::atomic<int64_t> filled{0};
    std::atomic<int64_t> pending{0};
  };

  static_assert(sizeof(Entry) == 64, "Entry must be one cache line");
  static_assert(alignof(Entry) == 64, "Entry must be cache-line aligned");

  std::array<Entry, kMaxSymbols> table_{};

  [[nodiscard]] static uint64_t symbolToKey(const char *data,
                                            std::size_t len) noexcept {
    uint64_t k = 0;
    const auto copy_len = (std::min)(len, std::size_t{8});
    if (copy_len > 0) {
      std::memcpy(&k, data, copy_len);
    }
    return k;
  }

  [[nodiscard]] static uint64_t symbolToKey(std::string_view symbol) noexcept {
    return symbolToKey(symbol.data(), symbol.size());
  }

  [[nodiscard]] static uint64_t symbolBufferToKey(const char *buffer) noexcept {
    uint64_t k = 0;
    std::memcpy(&k, buffer, 8);
    return k;
  }

  [[nodiscard]] static uint32_t hash(uint64_t key) noexcept {
    // splitmix64 finalizer — excellent distribution
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdULL;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ULL;
    key ^= key >> 33;
    return static_cast<uint32_t>(key);
  }

  [[nodiscard]] Entry *findOrInsert(uint64_t symbol_key) noexcept;
  [[nodiscard]] Entry *findMutable(uint64_t symbol_key) noexcept;
  [[nodiscard]] const Entry *find(uint64_t symbol_key) const noexcept;
};
