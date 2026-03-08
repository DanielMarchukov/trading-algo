#include "RiskManager.hpp"
#include <cstdlib>

RiskManager::RiskManager(
    const std::shared_ptr<PositionManager> &position_manager)
    : position_manager_(position_manager) {
  // TODO: Load parameters from some config.
}

bool RiskManager::onNewOrder(const Order &order) {
  if (!position_manager_) [[unlikely]] {
    return false;
  }

  const int64_t total_exposure =
      position_manager_->getTotalExposure(order.symbol);
  int64_t new_exposure = total_exposure;
  if (order.side == OrderSide::Buy) {
    new_exposure += order.quantity;
  } else {
    new_exposure -= order.quantity;
  }

  if (std::llabs(new_exposure) > max_position_per_symbol_) [[unlikely]] {
    return false;
  }

  if (order.price > 0 && order.quantity != 0) {
    const long double notional =
        static_cast<long double>(order.price) *
        static_cast<long double>(std::llabs(order.quantity));
    const long double limit = static_cast<long double>(max_order_value_) *
                              static_cast<long double>(SCALING_FACTOR);
    if (notional > limit) [[unlikely]] {
      return false;
    }
  }

  position_manager_->onOrderSent(order);
  return true;
}
