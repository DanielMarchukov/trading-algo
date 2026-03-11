#include "PositionManager.hpp"

PositionManager::Entry *
PositionManager::findOrInsert(uint64_t symbol_key) noexcept {
  const uint32_t start = hash(symbol_key) & kMask;
  for (uint32_t i = 0; i < kMaxSymbols; ++i) {
    auto &entry = table_[(start + i) & kMask];
    uint64_t expected = entry.key.load(std::memory_order_acquire);

    if (expected == symbol_key) {
      return &entry;
    }

    if (expected == kEmptySlot) {
      if (entry.key.compare_exchange_strong(expected, symbol_key,
                                            std::memory_order_release,
                                            std::memory_order_acquire)) {
        return &entry;
      }
      // CAS failed — another thread claimed this slot
      if (expected == symbol_key) {
        return &entry;
      }
      // Different key was inserted — continue probing
    }
  }
  return nullptr; // table full
}

PositionManager::Entry *
PositionManager::findMutable(uint64_t symbol_key) noexcept {
  const uint32_t start = hash(symbol_key) & kMask;
  for (uint32_t i = 0; i < kMaxSymbols; ++i) {
    auto &entry = table_[(start + i) & kMask];
    const uint64_t k = entry.key.load(std::memory_order_acquire);

    if (k == symbol_key) {
      return &entry;
    }
    if (k == kEmptySlot) {
      return nullptr;
    }
  }
  return nullptr;
}

const PositionManager::Entry *
PositionManager::find(uint64_t symbol_key) const noexcept {
  const uint32_t start = hash(symbol_key) & kMask;
  for (uint32_t i = 0; i < kMaxSymbols; ++i) {
    const auto &entry = table_[(start + i) & kMask];
    const uint64_t k = entry.key.load(std::memory_order_acquire);

    if (k == symbol_key) {
      return &entry;
    }
    if (k == kEmptySlot) {
      return nullptr;
    }
  }
  return nullptr;
}

void PositionManager::registerSymbol(std::string_view symbol) {
  (void)findOrInsert(symbolToKey(symbol));
}

void PositionManager::onFill(const Fill &fill) {
  Entry *entry = findOrInsert(symbolBufferToKey(fill.symbol));
  if (!entry) [[unlikely]] {
    return;
  }

  int64_t delta = fill.quantity;
  if (fill.side == OrderSide::Sell) {
    delta = -delta;
  }

  entry->filled.fetch_add(delta, std::memory_order_relaxed);
  entry->pending.fetch_sub(delta, std::memory_order_relaxed);
}

void PositionManager::onOrderSent(const Order &order) noexcept {
  Entry *entry = findOrInsert(symbolBufferToKey(order.symbol));
  if (!entry) [[unlikely]] {
    return;
  }

  int64_t delta = order.quantity;
  if (order.side == OrderSide::Sell) {
    delta = -delta;
  }

  entry->pending.fetch_add(delta, std::memory_order_relaxed);
}

void PositionManager::onOrderCancelled(const Order &order) {
  Entry *entry = findMutable(symbolBufferToKey(order.symbol));
  if (!entry) [[unlikely]] {
    return;
  }

  int64_t delta = order.quantity;
  if (order.side == OrderSide::Sell) {
    delta = -delta;
  }

  entry->pending.fetch_sub(delta, std::memory_order_relaxed);
}

int64_t PositionManager::getFilledPosition(std::string_view symbol) const {
  const Entry *entry = find(symbolToKey(symbol));
  if (!entry) {
    return 0;
  }
  return entry->filled.load(std::memory_order_relaxed);
}

int64_t PositionManager::getPendingPosition(std::string_view symbol) const {
  const Entry *entry = find(symbolToKey(symbol));
  if (!entry) {
    return 0;
  }
  return entry->pending.load(std::memory_order_relaxed);
}

int64_t
PositionManager::getTotalExposure(std::string_view symbol) const noexcept {
  const Entry *entry = find(symbolToKey(symbol));
  if (!entry) {
    return 0;
  }
  return entry->filled.load(std::memory_order_relaxed) +
         entry->pending.load(std::memory_order_relaxed);
}
