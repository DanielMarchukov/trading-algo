#include "MarketEventConsumer.hpp"
#include "Order.hpp"
#include "SimpleMarketMakingStrategy.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

std::atomic<bool> is_running(true);

void signal_handler(int signum) {
    std::cout << "Signal " << signum << " received." << std::endl;
    is_running.store(false);
}

using MarketMaking = SimpleMarketMakingStrategy;

void pin_thread_to_core(std::thread &t, int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    int rc =
        pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
    }
}

int main() {
    std::signal(SIGINT, signal_handler);

    const std::string address = "ipc:///tmp/market_data.sock";
    std::vector<std::string> symbols = {"AAPL", "GOOGL", "AMZN"};

    struct ConsumerThread {
        std::thread thread;
        std::unique_ptr<MarketEventConsumer<MarketMaking>> consumer;
    };
    std::vector<ConsumerThread> consumer_threads;

    for (size_t i = 0; i < symbols.size(); ++i) {
        auto callback = [symbol =
                             symbols[i]](const std::vector<Order> &orders) {
            std::cout << "--- Orders for " << symbol << " ---" << std::endl;
            for (const auto &order : orders) {
                std::cout << "  ID: " << order.id << ", Side: "
                          << (order.side == OrderSide::Buy ? "Buy" : "Sell")
                          << ", Qty: " << order.quantity
                          << ", Px: " << order.price << std::endl;
            }
        };

        auto consumer = std::make_unique<MarketEventConsumer<MarketMaking>>(
            address, symbols[i], is_running, callback);

        consumer_threads.push_back({
            std::thread([&consumer_threads, i = consumer_threads.size()]() {
                consumer_threads[i].consumer->run();
            }),
            std::move(consumer),
        });

        pin_thread_to_core(consumer_threads.back().thread, i + 1);
    }

    while (is_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    for (auto &t : consumer_threads) {
        if (t.thread.joinable()) {
            t.thread.join();
        }
    }

    return 0;
}
