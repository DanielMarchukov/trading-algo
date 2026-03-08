#include "AlpacaRestClient.hpp"
#include <cstring>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

AlpacaRestClient::AlpacaRestClient() {
  const char *api_key_cstr = std::getenv("APCA_API_KEY_ID");
  const char *api_secret_cstr = std::getenv("APCA_API_SECRET_KEY");
  const char *base_url_cstr = std::getenv("APCA_API_BASE_URL");

  if (!api_key_cstr || !api_secret_cstr) {
    throw std::runtime_error("FATAL: APCA_API_KEY_ID and/or "
                             "APCA_API_SECRET_KEY not set in environment.");
  }

  api_key_ = api_key_cstr;
  api_secret_ = api_secret_cstr;

  if (base_url_cstr) {
    base_url_ = cpr::Url{base_url_cstr};
  } else {
    base_url_ = cpr::Url{"https://paper-api.alpaca.markets"};
  }
}

std::expected<OrderAck, OrderError>
AlpacaRestClient::placeOrder(const Order &order) {
  nlohmann::json payload;
  payload["symbol"] = std::string_view(
      order.symbol, strnlen(order.symbol, sizeof(order.symbol)));
  payload["qty"] = std::to_string(order.quantity);
  payload["side"] = (order.side == OrderSide::Buy) ? "buy" : "sell";
  payload["type"] = (order.type == OrderType::Market) ? "market" : "limit";
  payload["time_in_force"] = "day";

  if (order.type == OrderType::Limit) {
    payload["limit_price"] =
        std::to_string(static_cast<double>(order.price) / SCALING_FACTOR);
  }

  const cpr::Response r =
      cpr::Post(cpr::Url{base_url_ + "/v2/orders"},
                cpr::Header{{"APCA-API-KEY-ID", api_key_},
                            {"APCA-API-SECRET-KEY", api_secret_},
                            {"Content-Type", "application/json"}},
                cpr::Body{payload.dump()});

  if (r.status_code >= 400) {
    return std::unexpected(
        OrderError{r.status_code, "Error placing order: " + r.text});
  }

  auto response = nlohmann::json::parse(r.text, nullptr, false);
  if (response.is_discarded()) {
    return std::unexpected(
        OrderError{r.status_code, "Invalid JSON in response: " + r.text});
  }

  return OrderAck{response.value("id", ""),
                  response.value("status", "unknown")};
}
