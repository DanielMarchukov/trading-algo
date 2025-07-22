#include "RiskManager.hpp"

RiskManager::RiskManager(const std::shared_ptr<PositionManager> &position_manager)
    : position_manager_(position_manager) {
    // TODO: Load parameters from some config.
}

bool RiskManager::onNewOrder(const Order &order) const {
    if (!position_manager_) {
        return false;
    }

    const int current_position = position_manager_->getPosition(order.symbol);
    int32_t new_position = current_position;
    if (order.side == OrderSide::Buy) {
        new_position += order.quantity;
    } else {
        new_position -= order.quantity;
    }

    if (std::abs(new_position) > max_position_per_symbol_) {
        return false;
    }

    if (order.price > 0 && (order.price * order.quantity) > max_order_value_ * SCALING_FACTOR) {
        return false;
    }
    return true;
}
