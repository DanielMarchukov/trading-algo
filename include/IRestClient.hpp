#pragma once

#include "Order.hpp"

class IRestClient {
public:
  virtual ~IRestClient() = default;

  virtual void placeOrder(const Order &order) = 0;
};
