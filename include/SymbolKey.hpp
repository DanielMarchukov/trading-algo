#pragma once

#include <cstring>
#include <functional>
#include <string_view>

struct SymbolKey {
  char value[8] = {};

  [[nodiscard]] bool operator==(const SymbolKey &other) const {
    return std::memcmp(value, other.value, sizeof(value)) == 0;
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
