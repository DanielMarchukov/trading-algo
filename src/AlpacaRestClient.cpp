#include "AlpacaRestClient.hpp"
#include <cstdlib>
#include <stdexcept>

AlpacaRestClient::AlpacaRestClient() {
    const char *api_key_cstr = std::getenv("APCA_API_KEY_ID");
    const char *api_secret_cstr = std::getenv("APCA_API_SECRET_KEY");
    if (!api_key_cstr || !api_secret_cstr) {
        throw std::runtime_error("FATAL: APCA_API_KEY_ID and/or "
                                 "APCA_API_SECRET_KEY not set in environment.");
    }

    std::string api_key = api_key_cstr;
    std::string api_secret = api_secret_cstr;
    session_.SetUrl(cpr::Url{base_url_ + "/v2/orders"});
    session_.SetHeader(
        {{"APCA-API-KEY-ID", api_key}, {"APCA-API-SECRET-KEY", api_secret}});
    session_.SetHeader({{"Content-Type", "application/json"}});
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
            std::to_string((double)order.price / SCALING_FACTOR);
    }
    json_payload_buffer_ = payload.dump();
    session_.SetBody(cpr::Body{json_payload_buffer_});
    cpr::Response r = session_.Post();
}
