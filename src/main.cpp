#include "MarketEvent.hpp"
#include "MarketEventConsumer.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <ql/quantlib.hpp>
#include <string>
#include <thread>
#include <vector>
#include <zmq.hpp>

std::atomic<bool> is_running(true);

void signal_handler(int signum) {
    std::cout << "Signal " << signum << " received." << std::endl;
    is_running.store(false);
}

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

void consumer_worker(const std::string &address, const std::string &symbol) {
    zmq::context_t context(1);
    zmq::socket_t subscriber(context, zmq::socket_type::sub);
    subscriber.set(zmq::sockopt::rcvtimeo, 500);
    subscriber.connect(address);
    subscriber.set(zmq::sockopt::subscribe, symbol);

    while (is_running.load()) {
        try {
            zmq::message_t topic;
            auto res1 = subscriber.recv(topic, zmq::recv_flags::none);
            if (!res1.has_value()) {
                continue;
            }
            zmq::message_t payload;
            auto res2 = subscriber.recv(payload, zmq::recv_flags::none);
            if (res2.has_value() && payload.size() == sizeof(MarketEvent)) {
                const MarketEvent *event = payload.data<MarketEvent>();
                std::cout << "ThreadID[" << std::this_thread::get_id() << "] "
                          << "Received event for " << symbol
                          << ": Type=" << static_cast<int>(event->eventType)
                          << ", TS=" << event->timestamp << ", P1=" << event->p1
                          << ", S1=" << event->s1 << ", P2(Ask)=" << event->p2
                          << ", S2(Ask)=" << event->s2
                          << ", ArrivedAt=" << event->arrivedAt << std::endl;
            }
        } catch (const zmq::error_t &e) {
            if (e.num() == ETERM) {
                break;
            }
            std::cerr << "Error: " << e.what() << std::endl;
            break;
        }
    }
}

int main() {
    std::signal(SIGINT, signal_handler);

    const std::string address = "ipc:///tmp/market_data.sock";
    std::vector<std::string> symbols = {"AAPL", "GOOGL", "AMZN"};
    std::vector<std::thread> threads;
    std::vector<std::unique_ptr<MarketEventConsumer>> consumers;

    for (size_t i = 0; i < symbols.size(); ++i) {
        consumers.push_back(std::make_unique<MarketEventConsumer>(
            address, symbols[i], is_running, nullptr));
        threads.emplace_back([&consumers, i]() { consumers[i]->run(); });
        pin_thread_to_core(threads.back(), i + 1);
    }

    while (is_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    for (auto &t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    return 0;
}
