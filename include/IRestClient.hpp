#pragma once

#include "Order.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

struct OrderAck {
  std::string client_order_id;
  std::string status;
};

struct OrderError {
  int64_t status_code;
  std::string message;
};

struct AlpacaOrderStatus {
  std::string id;
  std::string symbol;
  std::string side;
  std::string status;
  int64_t qty{0};
  int64_t filled_qty{0};
  double filled_avg_price{0.0};
};

class IRestClient {
public:
  virtual ~IRestClient() = default;

  [[nodiscard]] virtual std::expected<OrderAck, OrderError>
  placeOrder(const Order &order) = 0;

  [[nodiscard]] virtual std::expected<void, OrderError>
  cancelOrder(std::string_view alpaca_order_id) = 0;

  [[nodiscard]] virtual std::expected<std::vector<AlpacaOrderStatus>,
                                      OrderError>
  queryOrders(std::string_view status_filter, std::string_view after) = 0;
};
