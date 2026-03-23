#pragma once

#include "IRestClient.hpp"
#include "Order.hpp"
#include "RateLimiter.hpp"
#include <cpr/cpr.h>
#include <expected>
#include <memory>
#include <string>

class AlpacaRestClient final : public IRestClient {
public:
  explicit AlpacaRestClient(
      const std::string &base_url = "",
      int64_t rate_limit_threshold = RateLimiter::kDefaultThreshold,
      ThrottlePolicy throttle_policy = ThrottlePolicy::Drop);
  [[nodiscard]] std::expected<OrderAck, OrderError>
  placeOrder(const Order &order) override;

  [[nodiscard]] std::expected<void, OrderError>
  cancelOrder(std::string_view alpaca_order_id) override;

  [[nodiscard]] std::expected<std::vector<AlpacaOrderStatus>, OrderError>
  queryOrders(std::string_view status_filter, std::string_view after) override;

  [[nodiscard]] const RateLimiter &rateLimiter() const;

private:
  void buildOrderPayload(const Order &order);
  void updateRateLimit(const cpr::Response &r);
  void reapplyQuickAck();

  std::unique_ptr<cpr::Session> session_;
  std::string order_url_;
  std::string cancel_url_prefix_;
  std::string payload_buf_;
  RateLimiter rate_limiter_;
};
