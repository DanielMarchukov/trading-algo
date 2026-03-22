#include "RiskManager.hpp"
#include "OrderCooldown.hpp"
#include "PositionManager.hpp"
#include <cstdint>
#include <cstring>
#include <string_view>

namespace {

/// Absolute value without UB on INT64_MIN. std::llabs(INT64_MIN) is undefined
/// because negating the most negative value overflows signed long long. Casting
/// to uint64_t first makes the negation well-defined (modular arithmetic).
[[nodiscard]] constexpr uint64_t safeAbs(int64_t v) noexcept {
  return v < 0 ? -static_cast<uint64_t>(v) : static_cast<uint64_t>(v);
}

} // namespace

RiskManager::RiskManager(PositionManager *position_manager,
                         OrderCooldown *order_cooldown)
    : position_manager_(position_manager), order_cooldown_(order_cooldown) {}

bool RiskManager::onNewOrder(const Order &order) {
  const std::string_view symbol(order.symbol,
                                strnlen(order.symbol, sizeof(order.symbol)));

  if (order_cooldown_ && !order_cooldown_->checkAndUpdate(order.symbol))
      [[unlikely]] {
    return false;
  }

  if (!position_manager_) [[unlikely]] {
    return false;
  }

  const int64_t total_exposure = position_manager_->getTotalExposure(symbol);
  int64_t new_exposure = total_exposure;
  if (order.side == OrderSide::Buy) {
    new_exposure += order.quantity;
  } else {
    new_exposure -= order.quantity;
  }

  if (safeAbs(new_exposure) > static_cast<uint64_t>(max_position_per_symbol_))
      [[unlikely]] {
    return false;
  }

  if (order.price > 0 && order.quantity != 0) {
    const long double notional =
        static_cast<long double>(order.price) *
        static_cast<long double>(safeAbs(order.quantity));
    const long double limit = static_cast<long double>(max_order_value_) *
                              static_cast<long double>(SCALING_FACTOR);
    if (notional > limit) [[unlikely]] {
      return false;
    }
  }

  position_manager_->onOrderSent(order);
  return true;
}
