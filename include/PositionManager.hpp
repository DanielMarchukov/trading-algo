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
  [[nodiscard]] int64_t getCostBasis(std::string_view symbol) const;
  [[nodiscard]] int64_t getRealizedPnl(std::string_view symbol) const;

private:
  static constexpr uint64_t kEmptySlot = UINT64_MAX;
  static constexpr uint32_t kMask = kMaxSymbols - 1;

  struct alignas(64) Entry {
    std::atomic<uint64_t> key{kEmptySlot};
    std::atomic<int64_t> filled{0};
    std::atomic<int64_t> pending{0};
    char pad0_[40]{};

    std::atomic<int64_t> cost_basis_total{0};
    std::atomic<int64_t> realized_pnl{0};
    char pad1_[48]{};
  };

  static_assert(sizeof(std::atomic<uint64_t>) == 8, "Unexpected atomic size");
  static_assert(sizeof(std::atomic<int64_t>) == 8, "Unexpected atomic size");
  static_assert(sizeof(Entry) == 128, "Entry must be two cache lines");
  static_assert(alignof(Entry) == 64, "Entry must be cache-line aligned");

  static_assert(sizeof(Order::symbol) == 8,
                "Order::symbol must be 8 bytes for unique key encoding");
  static_assert(sizeof(Fill::symbol) == 8,
                "Fill::symbol must be 8 bytes for unique key encoding");

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
