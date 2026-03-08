#pragma once

#include "Fill.hpp"
#include "Order.hpp"
#include <cstring>
#include <functional>
#include <string_view>
#include <tbb/concurrent_hash_map.h>

struct SymbolKey {
  char value[8] = {};

  [[nodiscard]] bool operator==(const SymbolKey &other) const {
    return strncmp(value, other.value, sizeof(value)) == 0;
  }
};

static_assert(sizeof(SymbolKey) == 8, "SymbolKey must be exactly 8 bytes");

struct SymbolKeyHashCompare {
  [[nodiscard]] static std::size_t hash(const SymbolKey &k) {
    return std::hash<std::string_view>{}(
        std::string_view(k.value, sizeof(k.value)));
  }

  [[nodiscard]] static bool equal(const SymbolKey &lhs, const SymbolKey &rhs) {
    return std::memcmp(lhs.value, rhs.value, sizeof(lhs.value)) == 0;
  }
};

struct PositionState {
  int64_t filled = 0;
  int64_t pending = 0;
};

static_assert(sizeof(PositionState) == 16, "PositionState must be 16 bytes");
static_assert(alignof(PositionState) == 8,
              "PositionState must be 8-byte aligned");

class PositionManager {
public:
  PositionManager() = default;

  void registerSymbol(std::string_view symbol);
  void onFill(const Fill &fill);
  void onOrderSent(const Order &order);
  void onOrderCancelled(const Order &order);

  [[nodiscard]] int64_t getFilledPosition(std::string_view symbol) const;
  [[nodiscard]] int64_t getPendingPosition(std::string_view symbol) const;
  [[nodiscard]] int64_t getTotalExposure(std::string_view symbol) const;

private:
  using PositionMap =
      tbb::concurrent_hash_map<SymbolKey, PositionState, SymbolKeyHashCompare>;

  [[nodiscard]] static SymbolKey makeKey(std::string_view symbol);
  [[nodiscard]] static SymbolKey makeKeyFromBuffer(const char *symbol_buffer);

  mutable PositionMap positions_;
};
