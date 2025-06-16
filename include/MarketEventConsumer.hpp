#ifndef MARKET_EVENT_CONSUMER_HPP
#define MARKET_EVENT_CONSUMER_HPP

#include "MarketEvent.hpp"
#include <atomic>
#include <string>
#include <zmq.hpp>

class MarketEventConsumer {
  public:
    MarketEventConsumer(const std::string &ipc_address,
                        const std::string &symbol,
                        std::atomic<bool> &is_running);

    void run();

  private:
    std::string ipc_address_;
    std::string symbol_;
    std::atomic<bool> &is_running_;
    zmq::context_t context_;
    zmq::socket_t subscriber_;
};

#endif // MARKET_EVENT_CONSUMER_HPP
