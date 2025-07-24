#include "AlpacaRestClient.hpp"
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

AlpacaRestClient::AlpacaRestClient() {
  const char *api_key_cstr = std::getenv("APCA_API_KEY_ID");
  const char *api_secret_cstr = std::getenv("APCA_API_SECRET_KEY");
  const char *base_url_cstr =
      std::getenv("APCA_API_BASE_URL"); // Allow overriding for paper/live

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

void AlpacaRestClient::placeOrder(const Order &order) {
  nlohmann::json payload;
  payload["symbol"] = std::string_view(order.symbol);
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
    std::cerr << "Error placing order: " << r.status_code << " - " << r.text
              << std::endl;
  } else {
    std::cout << "Successfully placed order: " << r.text << std::endl;
  }
}
