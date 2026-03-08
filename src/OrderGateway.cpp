#include "OrderGateway.hpp"
#include <iostream>

OrderGateway::OrderGateway(
    std::atomic<bool> &is_running,
    const std::shared_ptr<LockFreeMPSCQueue<Order>> &order_queue,
    std::unique_ptr<IRestClient> rest_client,
    std::shared_ptr<PositionManager> position_manager)
    : is_running_(is_running), order_queue_(order_queue),
      rest_client_(std::move(rest_client)),
      position_manager_(std::move(position_manager)) {}

void OrderGateway::executeOrder(const Order &order) const {
  try {
    auto result = rest_client_->placeOrder(order);
    if (result) {
      std::cerr << "OrderGateway: Placed order " << result->client_order_id
                << " status=" << result->status << std::endl;
    } else {
      std::cerr << "OrderGateway: Rejected (HTTP " << result.error().status_code
                << "): " << result.error().message << std::endl;
      position_manager_->onOrderCancelled(order);
    }
  } catch (const std::exception &e) {
    std::cerr << "OrderGateway: Exception placing order: " << e.what()
              << std::endl;
    position_manager_->onOrderCancelled(order);
  } catch (...) {
    std::cerr << "OrderGateway: Unknown error placing order" << std::endl;
    position_manager_->onOrderCancelled(order);
  }
}

void OrderGateway::run() {
  Order order_to_execute{};
  while (order_queue_->wait_and_pop(order_to_execute, is_running_)) {
    order_to_execute.id = ++order_id_counter_;
    executeOrder(order_to_execute);
  }

  Order remaining_order{};
  while (order_queue_->try_pop(remaining_order)) {
    remaining_order.id = ++order_id_counter_;
    executeOrder(remaining_order);
  }
}
