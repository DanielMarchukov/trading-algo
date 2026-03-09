#pragma once

#include "MarketEvent.hpp"
#include "RiskManager.hpp"
#include "Strategy.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <zmq.hpp>

template <StrategyLike StrategyType, typename OrderCallbackType>
class MarketEventConsumer {
public:
  MarketEventConsumer(zmq::context_t &context, const std::string &address,
                      const std::string &symbol, std::atomic<bool> &is_running,
                      OrderCallbackType order_callback,
                      RiskManager *risk_manager)
      : subscriber_(context, zmq::socket_type::sub), is_running_(is_running),
        order_callback_(order_callback),
        strategy_(std::make_unique<StrategyType>()),
        risk_manager_(risk_manager) {
    std::memset(symbol_, 0, sizeof(symbol_));
    const auto n = (std::min)(symbol.size(), std::size_t{8});
    std::memcpy(symbol_, symbol.data(), n);

    try {
      subscriber_.set(zmq::sockopt::rcvtimeo, 100);
      subscriber_.set(zmq::sockopt::subscribe, std::string_view(symbol_, n));
      subscriber_.connect(address);
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

      MarketEvent event{};
      std::memcpy(&event, payload.data(), sizeof(MarketEvent));
      try {
        auto order = strategy_->onMarketEvent(event);
        if (order && risk_manager_->onNewOrder(*order)) {
          order_callback_(*order);
        }
      } catch (const std::exception &e) {
        std::cerr << "MarketEventConsumer[" << symbol_
                  << "]: exception in strategy/risk pipeline: " << e.what()
                  << std::endl;
      } catch (...) {
        std::cerr << "MarketEventConsumer[" << symbol_
                  << "]: unknown exception in strategy/risk pipeline"
                  << std::endl;
      }
    }
  }

private:
  zmq::socket_t subscriber_;
  char symbol_[9];
  std::atomic<bool> &is_running_;
  OrderCallbackType order_callback_;
  std::unique_ptr<StrategyType> strategy_;
  RiskManager *risk_manager_;
};
