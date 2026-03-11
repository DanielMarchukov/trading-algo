#include "OrderGateway.hpp"
#include "LatencyTracker.hpp"
#include "PendingOrderTracker.hpp"
#include "PositionManager.hpp"
#include "SymbolKey.hpp"
#include "Utils.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>

OrderGateway::OrderGateway(std::atomic<bool> &is_running,
                           LockFreeMPSCQueue<Order> *order_queue,
                           std::unique_ptr<IRestClient> rest_client,
                           PositionManager *position_manager,
                           PendingOrderTracker *pending_tracker,
                           LatencyTracker *latency_tracker)
    : is_running_(is_running), order_queue_(order_queue),
      rest_client_(std::move(rest_client)), position_manager_(position_manager),
      pending_tracker_(pending_tracker), latency_tracker_(latency_tracker) {
  if (order_queue_ == nullptr) {
    throw std::invalid_argument("OrderGateway: order_queue must not be null");
  }
  if (rest_client_ == nullptr) {
    throw std::invalid_argument("OrderGateway: rest_client must not be null");
  }
  if (position_manager_ == nullptr) {
    throw std::invalid_argument(
        "OrderGateway: position_manager must not be null");
  }
}

void OrderGateway::executeOrder(const Order &order) {
  SymbolKey key{};
  std::memcpy(key.value, order.symbol, sizeof(key.value));

  if (pending_tracker_) {
    auto prev_id = pending_tracker_->getExistingOrder(key, order.side);
    if (prev_id) {
      bool cancel_accepted = false;
      try {
        auto cancel_result = rest_client_->cancelOrder(prev_id->view());
        if (cancel_result) {
          std::cerr << "OrderGateway: Canceled previous order "
                    << prev_id->view() << '\n';
          cancel_accepted = true;
        } else {
          const auto code = cancel_result.error().status_code;
          std::cerr << "OrderGateway: Cancel returned HTTP " << code << " for "
                    << prev_id->view() << ": " << cancel_result.error().message
                    << '\n';
          if (code == 422) {
            cancel_accepted = true;
          }
        }
      } catch (const std::exception &e) {
        std::cerr << "OrderGateway: Exception canceling order "
                  << prev_id->view() << ": " << e.what() << '\n';
      } catch (...) {
        std::cerr << "OrderGateway: Unknown error canceling order "
                  << prev_id->view() << '\n';
      }
      if (!cancel_accepted) {
        std::cerr << "OrderGateway: Skipping new order — cancel for "
                  << prev_id->view() << " did not reach Alpaca" << '\n';
        position_manager_->onOrderCancelled(order);
        return;
      }
    }
  }

  try {
    auto result = rest_client_->placeOrder(order);
    if (result) {
      std::cerr << "OrderGateway: Placed order " << result->client_order_id
                << " status=" << result->status << '\n';
      if (pending_tracker_) {
        pending_tracker_->recordOrder(key, order.side, result->client_order_id);
      }
      if (latency_tracker_) {
        latency_tracker_->recordOrderSubmit(result->client_order_id);
      }
    } else {
      std::cerr << "OrderGateway: Rejected (HTTP " << result.error().status_code
                << "): " << result.error().message << '\n';
      position_manager_->onOrderCancelled(order);
    }
  } catch (const std::exception &e) {
    std::cerr << "OrderGateway: Exception placing order: " << e.what() << '\n';
    position_manager_->onOrderCancelled(order);
  } catch (...) {
    std::cerr << "OrderGateway: Unknown error placing order" << '\n';
    position_manager_->onOrderCancelled(order);
  }
}

void OrderGateway::run() {
  Order order_to_execute{};
  while (order_queue_->wait_and_pop(order_to_execute, is_running_)) {
    const uint64_t dequeued_at = nowNanos();
    order_to_execute.id = ++order_id_counter_;

    if (latency_tracker_) {
      latency_tracker_->record(LatencyMetric::MpscQueue,
                               dequeued_at - order_to_execute.queuedAt);
      latency_tracker_->record(LatencyMetric::EndToEnd,
                               dequeued_at - order_to_execute.arrivedAt);
    }

    executeOrder(order_to_execute);
  }

  Order remaining_order{};
  while (order_queue_->try_pop(remaining_order)) {
    const uint64_t dequeued_at = nowNanos();
    remaining_order.id = ++order_id_counter_;

    if (latency_tracker_) {
      latency_tracker_->record(LatencyMetric::MpscQueue,
                               dequeued_at - remaining_order.queuedAt);
      latency_tracker_->record(LatencyMetric::EndToEnd,
                               dequeued_at - remaining_order.arrivedAt);
    }

    executeOrder(remaining_order);
  }
}
