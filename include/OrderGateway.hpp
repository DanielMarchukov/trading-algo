#pragma once

#include "IRestClient.hpp"
#include "LockFreeMPSCQueue.hpp"
#include "Order.hpp"
#include "PositionManager.hpp"
#include <atomic>
#include <memory>

class OrderGateway {
public:
  OrderGateway(std::atomic<bool> &is_running,
               const std::shared_ptr<LockFreeMPSCQueue<Order>> &order_queue,
               std::unique_ptr<IRestClient> rest_client,
               std::shared_ptr<PositionManager> position_manager);

  void run();

private:
  void executeOrder(const Order &order) const;

  std::atomic<bool> &is_running_;
  std::shared_ptr<LockFreeMPSCQueue<Order>> order_queue_;
  std::unique_ptr<IRestClient> rest_client_;
  std::shared_ptr<PositionManager> position_manager_;
  uint64_t order_id_counter_ = 0;
};
