#include "PositionManager.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <cstring>

namespace {

constexpr std::size_t kSymbolCapacity = sizeof(SymbolKey{}.value);

} // namespace

SymbolKey PositionManager::makeKey(std::string_view symbol) {
  SymbolKey key{};
  const auto copy_len = std::min(symbol.size(), kSymbolCapacity);
  if (copy_len > 0) {
    std::memcpy(key.value, symbol.data(), copy_len);
  }
  return key;
}

SymbolKey PositionManager::makeKeyFromBuffer(const char *symbol_buffer) {
  return makeKey(std::string_view(symbol_buffer, kSymbolCapacity));
}

void PositionManager::registerSymbol(std::string_view symbol) {
  const SymbolKey key = makeKey(symbol);
  PositionMap::accessor accessor;
  const bool inserted = positions_.insert(accessor, key);
  if (inserted) {
    accessor->second = 0;
  }
}

void PositionManager::onFill(const Fill &fill) {
  const SymbolKey key = makeKeyFromBuffer(fill.symbol);
  PositionMap::accessor accessor;
  const bool inserted = positions_.insert(accessor, key);
  if (inserted) {
    accessor->second = 0;
  }

  int64_t &position = accessor->second;
  if (fill.side == OrderSide::Buy) {
    position += fill.quantity;
  } else {
    position -= fill.quantity;
  }
}

int64_t PositionManager::getPosition(const std::string_view symbol) const {
  const SymbolKey key = makeKey(symbol);
  PositionMap::const_accessor accessor;
  if (!positions_.find(accessor, key)) {
    return 0;
  }

  return accessor->second;
}
