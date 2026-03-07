#pragma once

#include "IRestClient.hpp"
#include "LockFreeMPSCQueue.hpp"
#include "Order.hpp"
#include <atomic>
#include <memory>

class OrderGateway {
public:
  OrderGateway(std::atomic<bool> &is_running,
               const std::shared_ptr<LockFreeMPSCQueue<Order>> &order_queue,
               std::unique_ptr<IRestClient> rest_client);

  void run() const;

private:
  std::atomic<bool> &is_running_;
  std::shared_ptr<LockFreeMPSCQueue<Order>> order_queue_;
  std::unique_ptr<IRestClient> rest_client_;
};
