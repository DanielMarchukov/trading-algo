#include "MarketEventConsumer.hpp"
#include <chrono>
#include <iostream>

MarketEventConsumer::MarketEventConsumer(const std::string &ipc_address,
                                         const std::string &symbol,
                                         std::atomic<bool> &is_running)
    : ipc_address_(ipc_address), symbol_(symbol), is_running_(is_running),
      context_(1), subscriber_(context_, zmq::socket_type::sub) {
    subscriber_.set(zmq::sockopt::rcvtimeo, 500);
    subscriber_.connect(ipc_address_);
    subscriber_.set(zmq::sockopt::subscribe, symbol_);
}

void MarketEventConsumer::run() {
    while (is_running_.load()) {
        try {
            zmq::message_t topic;
            if (!subscriber_.recv(topic, zmq::recv_flags::none)) {
                continue;
            }

            zmq::message_t payload;
            if (subscriber_.recv(payload, zmq::recv_flags::none) &&
                payload.size() == sizeof(MarketEvent)) {
                const MarketEvent *event = payload.data<MarketEvent>();
                uint64_t now_ns =
                    std::chrono::time_point_cast<std::chrono::nanoseconds>(
                        std::chrono::high_resolution_clock::now())
                        .time_since_epoch()
                        .count();

                uint64_t publisher_latency = now_ns - event->arrivedAt;

                std::cout << "C++ [" << symbol_
                          << "]: Received event. Latency from publisher: "
                          << publisher_latency << " ns." << std::endl;
                // TODO:
                // 1. Pass the 'event' to a Strategy object.
                // 2. The strategy returns a potential order.
                // 3. Pass the order to the RiskManager, etc.
            }
        } catch (const zmq::error_t &e) {
            if (e.num() == ETERM) {
                break;
            }
            std::cerr << "ZMQ Error in worker [" << symbol_ << "]: " << e.what()
                      << std::endl;
            break;
        }
    }
}
