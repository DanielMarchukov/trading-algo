#include "PendingOrderTracker.hpp"

std::optional<std::string>
PendingOrderTracker::getExistingOrder(const SymbolKey &symbol,
                                      OrderSide side) const {
  OrderSlotKey key{symbol, side};
  SlotMap::const_accessor acc;
  if (slots_.find(acc, key)) {
    return acc->second;
  }
  return std::nullopt;
}

void PendingOrderTracker::recordOrder(const SymbolKey &symbol, OrderSide side,
                                      std::string_view alpaca_order_id) {
  OrderSlotKey key{symbol, side};
  SlotMap::accessor acc;
  slots_.insert(acc, key);
  acc->second = std::string(alpaca_order_id);
}

void PendingOrderTracker::onOrderCompleted(const SymbolKey &symbol,
                                           OrderSide side,
                                           std::string_view alpaca_order_id) {
  OrderSlotKey key{symbol, side};
  SlotMap::accessor acc;
  if (slots_.find(acc, key) && acc->second == alpaca_order_id) {
    slots_.erase(acc);
  }
}
