#include "RiskManager.hpp"
#include <cstdlib>

RiskManager::RiskManager(
    const std::shared_ptr<PositionManager> &position_manager)
    : position_manager_(position_manager) {
  // TODO: Load parameters from some config.
}

bool RiskManager::onNewOrder(const Order &order) const {
  if (!position_manager_) {
    return false;
  }

  const int64_t current_position = position_manager_->getPosition(order.symbol);
  int64_t new_position = current_position;
  if (order.side == OrderSide::Buy) {
    new_position += order.quantity;
  } else {
    new_position -= order.quantity;
  }

  if (std::llabs(new_position) > max_position_per_symbol_) {
    return false;
  }

  if (order.price > 0 && order.quantity != 0) {
    const long double notional =
        static_cast<long double>(order.price) *
        static_cast<long double>(std::llabs(order.quantity));
    const long double limit = static_cast<long double>(max_order_value_) *
                              static_cast<long double>(SCALING_FACTOR);
    if (notional > limit) {
      return false;
    }
  }
  return true;
}
