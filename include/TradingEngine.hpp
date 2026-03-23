#pragma once

#include "AlpacaPipeline.hpp"
#include "AppConfig.hpp"
#include "IRestClient.hpp"
#include "LatencyTracker.hpp"
#include "LockFreeMPSCQueue.hpp"
#include "MarketEventConsumer.hpp"
#include "Order.hpp"
#include "OrderCooldown.hpp"
#include "OrderGateway.hpp"
#include "PendingOrderTracker.hpp"
#include "PositionManager.hpp"
#include "SimpleMarketMakingStrategy.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace spdlog {
class logger;
} // namespace spdlog

class IFillListener;

struct OrderQueuePusher {
  LockFreeMPSCQueue<Order> *queue;
  void operator()(const Order &order) const { queue->push(order); }
};

static_assert(std::is_trivially_copyable_v<OrderQueuePusher>,
              "OrderQueuePusher must be trivially copyable for hot-path use");

using ConsumerType =
    MarketEventConsumer<SimpleMarketMakingStrategy, OrderQueuePusher>;

static_assert(sizeof(OrderQueuePusher) == sizeof(void *),
              "OrderQueuePusher should be pointer-sized");
static_assert(alignof(ConsumerType) == 64,
              "ConsumerType must be cache-line aligned for hot-path use");
static_assert(sizeof(ConsumerType) % 64 == 0,
              "ConsumerType size must be a multiple of the cache line");

class TradingEngine {
public:
  explicit TradingEngine(
      const AppConfig &config, std::unique_ptr<IRestClient> rest_client,
      std::unique_ptr<IRestClient> reconciliation_client = nullptr);

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
  std::atomic<bool> shutdown_done_{false};
  AppConfig config_;
  std::string ipc_address_;
  std::vector<std::string> symbols_;
  std::unique_ptr<LatencyTracker> latency_tracker_;
  std::unique_ptr<PositionManager> position_manager_;
  std::unique_ptr<OrderCooldown> order_cooldown_;
  std::unique_ptr<RiskManager> risk_manager_;
  zmq::context_t context_{1};
  std::unique_ptr<PendingOrderTracker> pending_tracker_;
  std::unique_ptr<LockFreeMPSCQueue<Order>> order_queue_;
  std::unique_ptr<OrderGateway> order_gateway_;
  std::thread order_gateway_thread_;
  std::vector<ConsumerThread> consumer_threads_;
  std::unique_ptr<IRestClient> reconciliation_client_;
  std::unique_ptr<IFillListener> fill_listener_;
  std::unique_ptr<AlpacaPipeline> market_publisher_;
  spdlog::logger *logger_;
};
