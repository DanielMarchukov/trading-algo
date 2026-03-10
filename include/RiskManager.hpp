#pragma once

#include "Order.hpp"

class OrderCooldown;
class PositionManager;

class RiskManager {
public:
  explicit RiskManager(PositionManager *position_manager,
                       OrderCooldown *order_cooldown = nullptr);

  [[nodiscard]] bool onNewOrder(const Order &order);

private:
  PositionManager *position_manager_;
  OrderCooldown *order_cooldown_;
  const int64_t max_position_per_symbol_ = 1000;
  const double max_order_value_ = 10000.00;
};
