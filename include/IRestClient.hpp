#pragma once

#include "Order.hpp"
#include <cstdint>
#include <expected>
#include <string>

struct OrderAck {
  std::string client_order_id;
  std::string status;
};

struct OrderError {
  int64_t status_code;
  std::string message;
};

class IRestClient {
public:
  virtual ~IRestClient() = default;

  [[nodiscard]] virtual std::expected<OrderAck, OrderError>
  placeOrder(const Order &order) = 0;

  [[nodiscard]] virtual std::expected<void, OrderError>
  cancelOrder(std::string_view alpaca_order_id) = 0;
};
