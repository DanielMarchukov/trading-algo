#include "TradingEngine.hpp"
#include "Order.hpp"
#include <chrono>
#include <csignal>
#include <iostream>

namespace {
std::atomic<bool> *g_is_running_ptr = nullptr;
void signal_handler(int signum) {
    if (g_is_running_ptr) {
        std::cout << "\nSignal " << signum
                  << " received. Initiating shutdown..." << std::endl;
        g_is_running_ptr->store(false);
    }
}
} // namespace

void pin_thread_to_core(std::thread &t, int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    if (pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset) !=
        0) {
        std::cerr << "Error: Could not pin thread to core " << core_id
                  << std::endl;
    }
}

TradingEngine::TradingEngine()
    : is_running_(true), ipc_address_("ipc:///tmp/market_data.sock"),
      symbols_({"AAPL", "GOOGL", "AMZN"}) {
    setup_signal_handler();
}

void TradingEngine::setup_signal_handler() {
    g_is_running_ptr = &is_running_;
    std::signal(SIGINT, signal_handler);
}

void TradingEngine::launch_consumers() {
    std::cout << "C++ Trading Engine starting up. Launching threads..."
              << std::endl;

    for (size_t i = 0; i < symbols_.size(); ++i) {
        auto callback = [symbol =
                             symbols_[i]](const std::vector<Order> &orders) {
            std::cout << "--- Orders for " << symbol << " ---" << std::endl;
            for (const auto &order : orders) {
                std::cout << "  ID: " << order.id << ", Side: "
                          << (order.side == OrderSide::Buy ? "Buy" : "Sell")
                          << ", Qty: " << order.quantity
                          << ", Px: " << order.price << std::endl;
            }
        };

        auto consumer =
            std::make_unique<MarketEventConsumer<SimpleMarketMakingStrategy>>(
                ipc_address_, symbols_[i], is_running_, callback);

        consumer_threads_.push_back(
            {std::thread(&MarketEventConsumer<SimpleMarketMakingStrategy>::run,
                         consumer.get()),
             std::move(consumer)});

        pin_thread_to_core(consumer_threads_.back().thread, i + 1);
        std::cout << "Pinned thread for " << symbols_[i] << " to CPU Core "
                  << (i + 1) << std::endl;
    }
}

void TradingEngine::main_loop() {
    while (is_running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void TradingEngine::shutdown() {
    for (auto &ct : consumer_threads_) {
        if (ct.thread.joinable()) {
            ct.thread.join();
        }
    }
}

void TradingEngine::run() {
    launch_consumers();
    main_loop();
    shutdown();
}
