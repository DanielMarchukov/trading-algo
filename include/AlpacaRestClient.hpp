#pragma once

#include "IAlpacaRestClient.hpp"
#include "Order.hpp"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <string>

class AlpacaRestClient : public IAlpacaRestClient {
  public:
    AlpacaRestClient();

    bool placeOrder(const Order &order) override;

  private:
    const std::string base_url_ = "https://paper-api.alpaca.markets";
    cpr::Session session_;
    std::string json_payload_buffer_;
};
