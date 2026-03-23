#pragma once

#include "Order.hpp"

class OrderCooldown;
class PositionManager;

class RiskManager {
public:
  explicit RiskManager(PositionManager *position_manager,
                       OrderCooldown *order_cooldown = nullptr,
                       int64_t max_position_per_symbol = 1000,
                       double max_order_value = 10000.00);

  [[nodiscard]] bool onNewOrder(const Order &order);

private:
  PositionManager *position_manager_;
  OrderCooldown *order_cooldown_;
  int64_t max_position_per_symbol_;
  double max_order_value_;
};
