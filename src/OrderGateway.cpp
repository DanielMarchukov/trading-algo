#include "OrderGateway.hpp"
#include <chrono>
#include <iostream>
#include <thread>

OrderGateway::OrderGateway(
    std::atomic<bool> &is_running,
    const std::shared_ptr<ThreadSafeQueue<Order>> &order_queue,
    std::unique_ptr<IRestClient> rest_client)
    : is_running_(is_running), order_queue_(order_queue),
      rest_client_(std::move(rest_client)) {}

void OrderGateway::run() const {
  while (is_running_.load()) {
    if (Order order_to_execute{}; order_queue_->try_pop(order_to_execute)) {
      try {
        rest_client_->placeOrder(order_to_execute);
      } catch (const std::exception &e) {
        std::cerr << "OrderGateway: Error placing order: " << e.what()
                  << std::endl;
      } catch (...) {
        std::cerr << "OrderGateway: Unknown error placing order" << std::endl;
      }

    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
}
