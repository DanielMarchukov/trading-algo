#pragma once

#include "IRestClient.hpp"
#include "Order.hpp"
#include <cpr/cpr.h>
#include <string>

class AlpacaRestClient : public IRestClient {
  public:
    AlpacaRestClient();
    void placeOrder(const Order &order) override;

  private:
    std::string api_key_;
    std::string api_secret_;
    cpr::Url base_url_;
};
