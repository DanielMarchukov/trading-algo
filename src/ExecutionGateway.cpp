#include "ExecutionGateway.hpp"
#include <chrono>
#include <cstring>
#include <iostream>

ExecutionGateway::ExecutionGateway(
    std::atomic<bool> &is_running,
    std::shared_ptr<ThreadSafeQueue<Order>> order_queue,
    std::unique_ptr<IAlpacaRestClient> alpaca_rest_client,
    std::shared_ptr<PositionManager> position_manager)
    : is_running_(is_running), order_queue_(order_queue),
      alpaca_rest_client_(std::move(alpaca_rest_client)),
      position_manager_(position_manager) {}

void ExecutionGateway::run() {
    while (is_running_.load()) {
        Order order;
        if (order_queue_->try_pop(order)) {
            std::cout << "Processing order " << order.id << std::endl;

            bool success = alpaca_rest_client_->placeOrder(order);

            if (success && position_manager_) {
                Fill fill{};
                fill.orderId = order.id;
                fill.executionId = std::chrono::high_resolution_clock::now()
                                       .time_since_epoch()
                                       .count();
                strncpy(fill.symbol, order.symbol, sizeof(fill.symbol) - 1);
                fill.side = order.side;
                fill.quantity = order.quantity;
                fill.price = order.price;

                position_manager_->onFill(fill);
                std::cout << "Updated positions for order " << order.id
                          << std::endl;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    std::cout << "Execution Gateway shutting down." << std::endl;
}
