#pragma once

#include "LatencyTracker.hpp"
#include "MarketEvent.hpp"
#include "RiskManager.hpp"
#include "Strategy.hpp"
#include "Utils.hpp"
#include <atomic>
#include <cstring>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <zmq.hpp>

template <StrategyLike StrategyType, typename OrderCallbackType>
class alignas(64) MarketEventConsumer {
public:
  MarketEventConsumer(zmq::context_t &context, const std::string &address,
                      const std::string &symbol, std::atomic<bool> &is_running,
                      OrderCallbackType order_callback,
                      RiskManager *risk_manager,
                      LatencyTracker *latency_tracker = nullptr)
      : is_running_(is_running), strategy_(std::make_unique<StrategyType>()),
        risk_manager_(risk_manager), latency_tracker_(latency_tracker),
        subscriber_(context, zmq::socket_type::sub),
        order_callback_(order_callback), symbol_{} {
    if (symbol.size() > 8) {
      throw std::invalid_argument(
          "MarketEventConsumer symbol must be <= 8 bytes");
    }
    std::memset(symbol_, 0, sizeof(symbol_));
    std::memcpy(symbol_, symbol.data(), symbol.size());

    try {
      subscriber_.set(zmq::sockopt::rcvtimeo, 100);
      subscriber_.set(zmq::sockopt::subscribe,
                      std::string_view(symbol_, symbol.size()));
      subscriber_.connect(address);
    } catch (const zmq::error_t &e) {
      spdlog::get("system")->error(
          "MarketEventConsumer for {} ZMQ error during construction: {}",
          symbol_, e.what());
      throw;
    }
  }

  void run() {
    while (is_running_.load()) {
      zmq::message_t topic;
      zmq::message_t payload;

      if (auto topic_res = subscriber_.recv(topic, zmq::recv_flags::none);
          !topic_res.has_value()) {
        continue;
      }

      if (auto payload_res = subscriber_.recv(payload, zmq::recv_flags::none);
          !payload_res.has_value()) {
        continue;
      }

      if (payload.size() != sizeof(MarketEvent)) [[unlikely]] {
        continue;
      }

      MarketEvent event{};
      std::memcpy(&event, payload.data(), sizeof(MarketEvent));

      uint64_t zmq_received_at = 0;
      if (latency_tracker_) [[likely]] {
        zmq_received_at = nowNanos();
        latency_tracker_->record(LatencyMetric::ZmqTransport,
                                 zmq_received_at - event.arrivedAt);
      }

      auto order = strategy_->onMarketEvent(event);
      if (order && risk_manager_->onNewOrder(*order)) {
        const uint64_t pre_queue_at = nowNanos();

        if (latency_tracker_) [[likely]] {
          latency_tracker_->record(LatencyMetric::StrategyRisk,
                                   pre_queue_at - zmq_received_at);
        }

        order->arrivedAt = event.arrivedAt;
        order->queuedAt = pre_queue_at;
        order_callback_(*order);
      }
    }
  }

private:
  std::atomic<bool> &is_running_;
  std::unique_ptr<StrategyType> strategy_;
  RiskManager *risk_manager_;
  LatencyTracker *latency_tracker_;
  zmq::socket_t subscriber_;
  OrderCallbackType order_callback_;
  char symbol_[9];
};
