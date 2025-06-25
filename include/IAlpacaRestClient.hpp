#pragma once

#include "Order.hpp"

class IAlpacaRestClient {
  public:
    virtual ~IAlpacaRestClient() = default;
    virtual bool placeOrder(const Order &order) = 0;
};
