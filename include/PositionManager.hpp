#pragma once

#include "Fill.hpp"
#include <atomic>
#include <cstring>
#include <string_view>
#include <unordered_map>

struct SymbolKey {
    char value[8] = {};

    bool operator==(const SymbolKey &other) const {
        return strncmp(value, other.value, sizeof(value)) == 0;
    }
};

struct SymbolKeyHash {
    std::size_t operator()(const SymbolKey &k) const {
        return std::hash<std::string_view>()(
            std::string_view(k.value, sizeof(k.value)));
    }
};

class PositionManager {
  public:
    PositionManager() = default;

    void onFill(const Fill &fill);
    int getPosition(std::string_view symbol) const;

  private:
    std::unordered_map<SymbolKey, std::atomic<int>, SymbolKeyHash> positions_;
};
