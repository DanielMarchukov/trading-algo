#pragma once

#include "IAlpacaRestClient.hpp"
#include "Order.hpp"
#include "PositionManager.hpp"
#include "ThreadSafeQueue.hpp"
#include <atomic>
#include <memory>

class ExecutionGateway {
  public:
    ExecutionGateway(std::atomic<bool> &is_running,
                     std::shared_ptr<ThreadSafeQueue<Order>> order_queue,
                     std::unique_ptr<IAlpacaRestClient> alpaca_rest_client,
                     std::shared_ptr<PositionManager> position_manager);

    void run();

  private:
    std::atomic<bool> &is_running_;
    std::shared_ptr<ThreadSafeQueue<Order>> order_queue_;
    std::unique_ptr<IAlpacaRestClient> alpaca_rest_client_;
    std::shared_ptr<PositionManager> position_manager_;
};
