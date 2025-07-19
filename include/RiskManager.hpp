#pragma once

#include "Order.hpp"
#include "PositionManager.hpp"
#include <memory>

class RiskManager {
  public:
    explicit RiskManager(const std::shared_ptr<PositionManager> &position_manager);

    bool onNewOrder(const Order &order) const;

  private:
    std::shared_ptr<PositionManager> position_manager_;
    const int max_position_per_symbol_ = 1000;
    const double max_order_value_ = 10000.00;
};
