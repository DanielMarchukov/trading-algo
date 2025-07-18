#pragma once

#include "IRestClient.hpp"
#include "MarketEventConsumer.hpp"
#include "OrderGateway.hpp"
#include "SimpleMarketMakingStrategy.hpp"
#include "ThreadSafeQueue.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

void pin_thread_to_core(std::thread &t, int core_id);

class TradingEngine {
  public:
    enum class Mode { Production, Test };

    explicit TradingEngine(const std::vector<std::string> &symbols,
                           std::unique_ptr<IRestClient> rest_client,
                           Mode mode = Mode::Production);

    ~TradingEngine();

    void run();
    void stop();

    zmq::context_t &getContext();
    const std::string &getIPCAddress() const;

  private:
    struct ConsumerThread {
        std::thread thread;
        std::unique_ptr<MarketEventConsumer<SimpleMarketMakingStrategy>>
            consumer;
    };

    void setup_signal_handler();
    void launch_gateway();
    void launch_consumers();
    void main_loop();
    void shutdown();

    std::atomic<bool> is_running_;
    std::string ipc_address_;
    std::vector<std::string> symbols_;
    std::shared_ptr<RiskManager> risk_manager_;
    zmq::context_t context_{1};
    std::shared_ptr<ThreadSafeQueue<Order>> order_queue_;
    std::unique_ptr<OrderGateway> order_gateway_;
    std::thread order_gateway_thread_;
    std::vector<ConsumerThread> consumer_threads_;
};
