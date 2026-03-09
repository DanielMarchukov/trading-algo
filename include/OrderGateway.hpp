#pragma once

#include "IRestClient.hpp"
#include "LockFreeMPSCQueue.hpp"
#include "Order.hpp"
#include <atomic>
#include <memory>

class PositionManager;

class OrderGateway {
public:
  OrderGateway(std::atomic<bool> &is_running,
               LockFreeMPSCQueue<Order> *order_queue,
               std::unique_ptr<IRestClient> rest_client,
               PositionManager *position_manager);

  void run();

private:
  void executeOrder(const Order &order) const;

  std::atomic<bool> &is_running_;
  LockFreeMPSCQueue<Order> *order_queue_;
  std::unique_ptr<IRestClient> rest_client_;
  PositionManager *position_manager_;
  uint64_t order_id_counter_ = 0;
};
