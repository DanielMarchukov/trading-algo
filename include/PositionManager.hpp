#pragma once

#include "Fill.hpp"
#include <cstring>
#include <functional>
#include <string_view>
#include <tbb/concurrent_hash_map.h>

struct SymbolKey {
  char value[8] = {};

  bool operator==(const SymbolKey &other) const {
    return strncmp(value, other.value, sizeof(value)) == 0;
  }
};

struct SymbolKeyHashCompare {
  static std::size_t hash(const SymbolKey &k) {
    return std::hash<std::string_view>{}(
        std::string_view(k.value, sizeof(k.value)));
  }

  static bool equal(const SymbolKey &lhs, const SymbolKey &rhs) {
    return std::memcmp(lhs.value, rhs.value, sizeof(lhs.value)) == 0;
  }
};

class PositionManager {
public:
  PositionManager() = default;

  void registerSymbol(std::string_view symbol);
  void onFill(const Fill &fill);
  [[nodiscard]] int64_t getPosition(std::string_view symbol) const;

private:
  using PositionMap =
      tbb::concurrent_hash_map<SymbolKey, int64_t, SymbolKeyHashCompare>;

  static SymbolKey makeKey(std::string_view symbol);
  static SymbolKey makeKeyFromBuffer(const char *symbol_buffer);

  mutable PositionMap positions_;
};
