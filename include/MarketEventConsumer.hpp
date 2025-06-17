#ifndef MARKET_EVENT_CONSUMER_HPP
#define MARKET_EVENT_CONSUMER_HPP

#include "MarketEvent.hpp"
#include <atomic>
#include <functional>
#include <string>
#include <zmq.hpp>

class MarketEventConsumer {
  public:
    using Callback = std::function<void(const MarketEvent &)>;

    MarketEventConsumer(const std::string &ipc_address,
                        const std::string &symbol,
                        std::atomic<bool> &is_running, Callback callback);

    void run();

  private:
    std::string ipc_address_;
    std::string symbol_;
    std::atomic<bool> &is_running_;
    Callback callback_;
    zmq::context_t context_;
    zmq::socket_t subscriber_;
};

#endif // MARKET_EVENT_CONSUMER_HPP
