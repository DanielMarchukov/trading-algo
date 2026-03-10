#pragma once

#include "Order.hpp"
#include "PositionManager.hpp"
#include <optional>
#include <string>
#include <tbb/concurrent_hash_map.h>

struct OrderSlotKey {
  SymbolKey symbol;
  OrderSide side;

  [[nodiscard]] bool operator==(const OrderSlotKey &other) const {
    return symbol == other.symbol && side == other.side;
  }
};

struct OrderSlotKeyHashCompare {
  [[nodiscard]] static std::size_t hash(const OrderSlotKey &k) {
    auto h = SymbolKeyHashCompare::hash(k.symbol);
    h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(k.side)) + 0x9e3779b9 +
         (h << 6) + (h >> 2);
    return h;
  }

  [[nodiscard]] static bool equal(const OrderSlotKey &lhs,
                                  const OrderSlotKey &rhs) {
    return lhs == rhs;
  }
};

class PendingOrderTracker {
public:
  [[nodiscard]] std::optional<std::string>
  getExistingOrder(const SymbolKey &symbol, OrderSide side) const;

  void recordOrder(const SymbolKey &symbol, OrderSide side,
                   std::string_view alpaca_order_id);

  void onOrderCompleted(const SymbolKey &symbol, OrderSide side,
                        std::string_view alpaca_order_id);

private:
  using SlotMap = tbb::concurrent_hash_map<OrderSlotKey, std::string,
                                           OrderSlotKeyHashCompare>;
  mutable SlotMap slots_;
};
