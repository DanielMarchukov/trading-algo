#pragma once

#include "Order.hpp"
#include "PositionManager.hpp"
#include <cstring>
#include <optional>
#include <string_view>
#include <tbb/concurrent_hash_map.h>
#include <type_traits>

struct OrderSlotKey {
  SymbolKey symbol;
  OrderSide side;

  [[nodiscard]] bool operator==(const OrderSlotKey &other) const {
    return symbol == other.symbol && side == other.side;
  }
};

static_assert(std::is_trivially_copyable_v<OrderSlotKey>,
              "OrderSlotKey must be trivially copyable for hash map");

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

struct AlpacaOrderId {
  static constexpr std::size_t kCapacity = 48;
  char value[kCapacity]{};

  AlpacaOrderId() = default;

  explicit AlpacaOrderId(std::string_view id) {
    const auto len = (std::min)(id.size(), kCapacity);
    std::memcpy(value, id.data(), len);
  }

  [[nodiscard]] std::string_view view() const {
    return {value, strnlen(value, kCapacity)};
  }

  [[nodiscard]] bool operator==(std::string_view other) const {
    return view() == other;
  }
};

static_assert(std::is_trivially_copyable_v<AlpacaOrderId>,
              "AlpacaOrderId must be trivially copyable");
static_assert(sizeof(AlpacaOrderId) == 48, "AlpacaOrderId must be 48 bytes");

class PendingOrderTracker {
public:
  [[nodiscard]] std::optional<AlpacaOrderId>
  getExistingOrder(const SymbolKey &symbol, OrderSide side) const;

  void recordOrder(const SymbolKey &symbol, OrderSide side,
                   std::string_view alpaca_order_id);

  void onOrderCompleted(const SymbolKey &symbol, OrderSide side,
                        std::string_view alpaca_order_id);

private:
  using SlotMap = tbb::concurrent_hash_map<OrderSlotKey, AlpacaOrderId,
                                           OrderSlotKeyHashCompare>;
  mutable SlotMap slots_;
};
