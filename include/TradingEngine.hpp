#pragma once
#include "MarketEventConsumer.hpp"
#include "SimpleMarketMakingStrategy.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class TradingEngine {
  public:
    TradingEngine();

    void run();

  private:
    struct ConsumerThread {
        std::thread thread;
        std::unique_ptr<MarketEventConsumer<SimpleMarketMakingStrategy>>
            consumer;
    };

    void setup_signal_handler();
    void launch_consumers();
    void main_loop();
    void shutdown();

    std::atomic<bool> is_running_;
    const std::string ipc_address_;
    const std::vector<std::string> symbols_;
    std::vector<ConsumerThread> consumer_threads_;
};
