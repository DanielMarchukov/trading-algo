#pragma once

#include "Order.hpp"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <string>

class AlpacaRestClient {
  public:
    AlpacaRestClient();
    ~AlpacaRestClient() = default;
    void placeOrder(const Order &order);

  private:
    const std::string base_url_ = "https://paper-api.alpaca.markets";
    cpr::Session session_;
    std::string json_payload_buffer_;
};
