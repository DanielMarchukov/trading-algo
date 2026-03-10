#pragma once

#include "IRestClient.hpp"
#include "Order.hpp"
#include <cpr/cpr.h>
#include <expected>
#include <string>

class AlpacaRestClient final : public IRestClient {
public:
  AlpacaRestClient();
  [[nodiscard]] std::expected<OrderAck, OrderError>
  placeOrder(const Order &order) override;

  [[nodiscard]] std::expected<void, OrderError>
  cancelOrder(std::string_view alpaca_order_id) override;

private:
  std::string api_key_;
  std::string api_secret_;
  cpr::Url base_url_;
};
