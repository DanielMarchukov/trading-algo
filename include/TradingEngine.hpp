#pragma once

#include "AlpacaPipeline.hpp"
#include "IRestClient.hpp"
#include "LockFreeMPSCQueue.hpp"
#include "MarketEventConsumer.hpp"
#include "Order.hpp"
#include "OrderGateway.hpp"
#include "PositionManager.hpp"
#include "SimpleMarketMakingStrategy.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class IFillListener;

struct OrderQueuePusher {
  LockFreeMPSCQueue<Order> *queue;
  void operator()(const Order &order) const { queue->push(order); }
};

using ConsumerType =
    MarketEventConsumer<SimpleMarketMakingStrategy, OrderQueuePusher>;

class TradingEngine {
public:
  explicit TradingEngine(const std::vector<std::string> &symbols,
                         std::unique_ptr<IRestClient> rest_client);

  ~TradingEngine();

  void run();
  void stop();

private:
  struct ConsumerThread {
    std::thread thread;
    std::unique_ptr<ConsumerType> consumer;
  };

  void setup_signal_handler();
  void launch_gateway();
  void launch_consumers();
  void main_loop() const;
  void shutdown();

  std::atomic<bool> is_running_;
  std::string ipc_address_;
  std::vector<std::string> symbols_;
  std::unique_ptr<PositionManager> position_manager_;
  std::unique_ptr<RiskManager> risk_manager_;
  zmq::context_t context_{1};
  std::unique_ptr<LockFreeMPSCQueue<Order>> order_queue_;
  std::unique_ptr<OrderGateway> order_gateway_;
  std::thread order_gateway_thread_;
  std::vector<ConsumerThread> consumer_threads_;
  std::unique_ptr<IFillListener> fill_listener_;
  std::unique_ptr<AlpacaPipeline> market_publisher_;
};
