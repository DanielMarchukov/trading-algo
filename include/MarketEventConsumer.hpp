#pragma once

#include "MarketEvent.hpp"
#include "RiskManager.hpp"
#include "Strategy.hpp"
#include <atomic>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <zmq.hpp>

template <StrategyLike StrategyType> class MarketEventConsumer {
public:
  using OrderCallback = std::function<void(const Order &)>;

  MarketEventConsumer(zmq::context_t &context, std::string address,
                      std::string symbol, std::atomic<bool> &is_running,
                      OrderCallback order_callback,
                      const std::shared_ptr<RiskManager> &risk_manager)
      : subscriber_(context, zmq::socket_type::sub),
        address_(std::move(address)), symbol_(std::move(symbol)),
        is_running_(is_running), order_callback_(std::move(order_callback)),
        strategy_(std::make_unique<StrategyType>()),
        risk_manager_(risk_manager) {
    try {
      subscriber_.set(zmq::sockopt::rcvtimeo, 100);
      subscriber_.set(zmq::sockopt::subscribe, symbol_);
      subscriber_.connect(address_);
    } catch (const zmq::error_t &e) {
      std::cerr << "MarketEventConsumer for " << symbol_
                << " ZMQ error during construction: " << e.what() << std::endl;
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

      auto event = static_cast<const MarketEvent *>(payload.data());
      try {
        auto batch = strategy_->onMarketEvent(*event);
        for (uint8_t i = 0; i < batch.count; ++i) {
          if (risk_manager_->onNewOrder(batch.orders[i])) {
            order_callback_(batch.orders[i]);
          }
        }
      } catch (...) {
      }
    }
  }

private:
  zmq::socket_t subscriber_;
  std::string address_;
  std::string symbol_;
  std::atomic<bool> &is_running_;
  OrderCallback order_callback_;
  std::unique_ptr<StrategyType> strategy_;
  std::shared_ptr<RiskManager> risk_manager_;
};
