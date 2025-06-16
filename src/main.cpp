#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
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

void runPublisher(const std::string &address);

#pragma pack(push, 1)
struct MarketEvent {
    uint8_t eventType;
    uint64_t timestamp;
    double price;  // Bid or Trade price
    uint32_t size; // Bid or Trade size
    double askPrice;
    uint32_t askSize;
    uint64_t arrivedAt;
    char padding[7];
};
#pragma pack(pop)

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
                std::cout << "Received event for " << symbol
                          << ": Type=" << static_cast<int>(event->eventType)
                          << ", TS=" << event->timestamp
                          << ", P1=" << event->price << ", S1=" << event->size
                          << ", P2(Ask)=" << event->askPrice
                          << ", S2(Ask)=" << event->askSize << std::endl;
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
    static_assert(sizeof(MarketEvent) == 48, "Struct size mismatch");

    const std::string address = "inproc://alpaca_data";
    std::vector<std::string> symbols = {"AAPL", "GOOGL", "AMZN"};
    std::vector<std::thread> consumers;

    for (size_t i = 0; i < symbols.size(); ++i) {
        consumers.emplace_back(consumer_worker, address, symbols[i]);
        pin_thread_to_core(consumers.back(), i + 1);
    }

    while (is_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    for (auto &t : consumers) {
        if (t.joinable()) {
            t.join();
        }
    }

    return 0;
}
