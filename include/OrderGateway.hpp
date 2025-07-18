#pragma once

#include "IRestClient.hpp"
#include "Order.hpp"
#include "ThreadSafeQueue.hpp"
#include <atomic>
#include <memory>

class OrderGateway {
  public:
    OrderGateway(std::atomic<bool> &is_running,
                 std::shared_ptr<ThreadSafeQueue<Order>> order_queue,
                 std::unique_ptr<IRestClient> rest_client);

    void run();

  private:
    std::atomic<bool> &is_running_;
    std::shared_ptr<ThreadSafeQueue<Order>> order_queue_;
    std::unique_ptr<IRestClient> rest_client_;
};
