#include "TradingEngine.hpp"
#include "Order.hpp"
#include "PositionManager.hpp"
#include <chrono>
#include <csignal>
#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__) || defined(__gnu_linux__)
#include <pthread.h>
#elif defined(__APPLE__)
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#endif

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
#if defined(__linux__) || defined(__gnu_linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    int rc =
        pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
    }
#elif defined(_WIN32)
    DWORD_PTR mask = 1LL << core_id;
    if (SetThreadAffinityMask(t.native_handle(), mask) == 0) {
        std::cerr << "Error calling SetThreadAffinityMask: " << GetLastError()
                  << std::endl;
    }
#elif defined(__APPLE__)
    thread_affinity_policy_data_t policy = {core_id};
    thread_port_t mach_thread = pthread_mach_thread_np(t.native_handle());
    if (thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY,
                          (thread_policy_t)&policy,
                          THREAD_AFFINITY_POLICY_COUNT) != KERN_SUCCESS) {
        std::cerr << "Error calling thread_policy_set" << std::endl;
    }
#else
    // For other systems, this is a no-op.
    (void)t; // Suppress unused parameter warning
    (void)core_id;
    std::cout << "Warning: CPU pinning not supported on this platform."
              << std::endl;
#endif
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

    auto risk_manager =
        std::make_shared<RiskManager>(std::make_shared<PositionManager>());

    for (size_t i = 0; i < symbols_.size(); ++i) {
        auto callback = [symbol = symbols_[i]](const Order &order) {
            std::cout << "--- Order for " << symbol << " ---" << std::endl;
            std::cout << "  ID: " << order.id << ", Side: "
                      << (order.side == OrderSide::Buy ? "Buy" : "Sell")
                      << ", Qty: " << order.quantity << ", Px: " << order.price
                      << std::endl;
        };

        auto consumer =
            std::make_unique<MarketEventConsumer<SimpleMarketMakingStrategy>>(
                ipc_address_, symbols_[i], is_running_, callback, risk_manager);

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
        // Main loop sleeps while spawned threads are processing market events.
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
