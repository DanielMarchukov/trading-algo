#pragma once

#include "IRestClient.hpp"
#include "LockFreeMPSCQueue.hpp"
#include "MarketEventConsumer.hpp"
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

void pin_thread_to_core(std::thread &t, uint32_t core_id);

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
    std::unique_ptr<MarketEventConsumer<SimpleMarketMakingStrategy>> consumer;
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
};
