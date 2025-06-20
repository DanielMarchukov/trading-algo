#pragma once

#include "MarketEvent.hpp"
#include "Order.hpp"
#include <atomic>
#include <functional>
#include <string>
#include <zmq.hpp>

template <typename StrategyType> class MarketEventConsumer {
  public:
    using Callback = std::function<void(const std::vector<Order> &)>;

    MarketEventConsumer(const std::string &ipc_address,
                        const std::string &symbol,
                        std::atomic<bool> &is_running, Callback callback)
        : ipc_address_(ipc_address), symbol_(symbol), is_running_(is_running),
          callback_(std::move(callback)), strategy_type_(), context_(1),
          subscriber_(context_, zmq::socket_type::sub) {
        subscriber_.set(zmq::sockopt::rcvtimeo, 500);
        subscriber_.connect(ipc_address_);
        subscriber_.set(zmq::sockopt::subscribe, symbol_);
    }

    void run() {
        while (is_running_.load()) {
            zmq::message_t topic;
            if (!subscriber_.recv(topic, zmq::recv_flags::none)) {
                continue;
            }

            zmq::message_t payload;
            if (subscriber_.recv(payload, zmq::recv_flags::none) &&
                payload.size() == sizeof(MarketEvent)) {
                const MarketEvent *event = payload.data<MarketEvent>();
                auto orders = strategy_type_.onMarketEvent(*event);
                if (orders.size() > 0 && callback_) {
                    callback_(orders);
                }
            }
        }
    }

  private:
    std::string ipc_address_;
    std::string symbol_;
    std::atomic<bool> &is_running_;
    Callback callback_;
    StrategyType strategy_type_;
    zmq::context_t context_;
    zmq::socket_t subscriber_;
};
