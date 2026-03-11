#include "AlpacaRestClient.hpp"
#include <cstring>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace {

std::string buildOrderPayload(const Order &order) {
  std::string payload;
  payload.reserve(256);
  payload += R"({"symbol":")";
  payload.append(order.symbol, strnlen(order.symbol, sizeof(order.symbol)));
  payload += R"(","qty":")";
  payload += std::to_string(order.quantity);
  payload += R"(","side":")";
  payload += (order.side == OrderSide::Buy) ? "buy" : "sell";

  if (order.type == OrderType::Market) {
    payload += R"(","type":"market","time_in_force":"day"})";
  } else {
    payload += R"(","type":"limit","time_in_force":"day","limit_price":")";
    const auto whole = order.price / SCALING_FACTOR;
    const auto frac = order.price % SCALING_FACTOR;
    payload += std::to_string(whole);
    payload += '.';
    auto frac_str = std::to_string(frac);
    payload.append(4 - frac_str.size(), '0');
    payload += frac_str;
    payload += R"("})";
  }
  return payload;
}

} // namespace

AlpacaRestClient::AlpacaRestClient() {
  const char *api_key_cstr = std::getenv("APCA_API_KEY_ID");
  const char *api_secret_cstr = std::getenv("APCA_API_SECRET_KEY");
  const char *base_url_cstr = std::getenv("APCA_API_BASE_URL");

  if (!api_key_cstr || !api_secret_cstr) {
    throw std::runtime_error("FATAL: APCA_API_KEY_ID and/or "
                             "APCA_API_SECRET_KEY not set in environment.");
  }

  const std::string base_url =
      base_url_cstr ? base_url_cstr : "https://paper-api.alpaca.markets";

  order_url_ = base_url + "/v2/orders";
  cancel_url_prefix_ = base_url + "/v2/orders/";

  session_ = std::make_unique<cpr::Session>();
  session_->SetHeader(cpr::Header{{"APCA-API-KEY-ID", api_key_cstr},
                                  {"APCA-API-SECRET-KEY", api_secret_cstr},
                                  {"Content-Type", "application/json"}});
}

std::expected<OrderAck, OrderError>
AlpacaRestClient::placeOrder(const Order &order) {
  auto payload = buildOrderPayload(order);

  session_->SetUrl(cpr::Url{order_url_});
  session_->SetBody(cpr::Body{std::move(payload)});
  const cpr::Response r = session_->Post();

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

std::expected<void, OrderError>
AlpacaRestClient::cancelOrder(std::string_view alpaca_order_id) {
  session_->SetUrl(cpr::Url{cancel_url_prefix_ + std::string(alpaca_order_id)});
  session_->RemoveContent();
  const cpr::Response r = session_->Delete();

  if (r.status_code == 204) {
    return {};
  }

  return std::unexpected(
      OrderError{r.status_code, "Error canceling order: " + r.text});
}
