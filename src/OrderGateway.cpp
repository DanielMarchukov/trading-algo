#include "OrderGateway.hpp"

OrderGateway::OrderGateway(std::atomic<bool> &is_running,
                           std::shared_ptr<ThreadSafeQueue<Order>> order_queue,
                           std::unique_ptr<IRestClient> rest_client)
    : is_running_(is_running), order_queue_(std::move(order_queue)),
      rest_client_(std::move(rest_client)) {}

void OrderGateway::run() {
    while (is_running_.load()) {
        Order order_to_execute;
        if (order_queue_->try_pop(order_to_execute)) {
            rest_client_->placeOrder(order_to_execute);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
