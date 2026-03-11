#pragma once

#include "IRestClient.hpp"
#include "Order.hpp"
#include <cpr/cpr.h>
#include <expected>
#include <memory>
#include <string>

class AlpacaRestClient final : public IRestClient {
public:
  AlpacaRestClient();
  [[nodiscard]] std::expected<OrderAck, OrderError>
  placeOrder(const Order &order) override;

  [[nodiscard]] std::expected<void, OrderError>
  cancelOrder(std::string_view alpaca_order_id) override;

private:
  void buildOrderPayload(const Order &order);

  std::unique_ptr<cpr::Session> session_;
  std::string order_url_;
  std::string cancel_url_prefix_;
  std::string payload_buf_;
};
