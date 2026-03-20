#include "AlpacaRestClient.hpp"
#include <charconv>
#include <cpr/curlholder.h>
#include <cstring>
#include <curl/curl.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

#if defined(__linux__)
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

namespace {

constexpr std::size_t kPriceDecimals = [] {
  std::size_t digits = 0;
  int64_t f = SCALING_FACTOR;
  while (f > 1) {
    f /= 10;
    ++digits;
  }
  return digits;
}();

static_assert(kPriceDecimals > 0);
static_assert(
    [] {
      int64_t f = SCALING_FACTOR;
      while (f > 1) {
        if (f % 10 != 0)
          return false;
        f /= 10;
      }
      return f == 1;
    }(),
    "SCALING_FACTOR must be a power of 10");

#if defined(__linux__)
void reapplyTcpQuickAck(CURL *handle) {
  curl_socket_t sockfd = CURL_SOCKET_BAD;
  curl_easy_getinfo(handle, CURLINFO_ACTIVESOCKET, &sockfd);
  if (sockfd != CURL_SOCKET_BAD) {
    int flag = 1;
    ::setsockopt(sockfd, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag));
  }
}

int setTcpQuickAck(void * /*clientp*/, curl_socket_t curlfd,
                   curlsocktype purpose) {
  if (purpose != CURLSOCKTYPE_IPCXN) {
    return CURL_SOCKOPT_OK;
  }
  int flag = 1;
  if (::setsockopt(curlfd, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag)) !=
      0) {
    return CURL_SOCKOPT_ERROR;
  }
  return CURL_SOCKOPT_OK;
}
#endif

} // namespace

AlpacaRestClient::AlpacaRestClient(int64_t rate_limit_threshold,
                                   ThrottlePolicy throttle_policy)
    : rate_limiter_(rate_limit_threshold, throttle_policy) {
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

  payload_buf_.reserve(256);

  session_ = std::make_unique<cpr::Session>();
  session_->SetHeader(cpr::Header{{"APCA-API-KEY-ID", api_key_cstr},
                                  {"APCA-API-SECRET-KEY", api_secret_cstr},
                                  {"Content-Type", "application/json"}});

#if defined(__linux__)
  auto holder = session_->GetCurlHolder();
  curl_easy_setopt(holder->handle, CURLOPT_SOCKOPTFUNCTION, setTcpQuickAck);
#endif
}

void AlpacaRestClient::buildOrderPayload(const Order &order) {
  payload_buf_.clear();
  payload_buf_ += R"({"symbol":")";
  payload_buf_.append(order.symbol,
                      strnlen(order.symbol, sizeof(order.symbol)));
  payload_buf_ += R"(","qty":")";
  payload_buf_ += std::to_string(order.quantity);
  payload_buf_ += R"(","side":")";
  payload_buf_ += (order.side == OrderSide::Buy) ? "buy" : "sell";

  if (order.type == OrderType::Market) {
    payload_buf_ += R"(","type":"market","time_in_force":"day"})";
  } else {
    payload_buf_ += R"(","type":"limit","time_in_force":"day","limit_price":")";
    const auto whole = order.price / SCALING_FACTOR;
    const auto frac = order.price % SCALING_FACTOR;
    payload_buf_ += std::to_string(whole);
    payload_buf_ += '.';
    auto frac_str = std::to_string(frac);
    if (frac_str.size() < kPriceDecimals) {
      payload_buf_.append(kPriceDecimals - frac_str.size(), '0');
    }
    payload_buf_ += frac_str;
    payload_buf_ += R"("})";
  }
}

void AlpacaRestClient::updateRateLimit(const cpr::Response &r) {
  if (r.status_code == 429) {
    rate_limiter_.onRateLimited();
    return;
  }
  auto it = r.header.find("X-Ratelimit-Remaining");
  if (it != r.header.end()) {
    int64_t remaining = 0;
    const auto &val = it->second;
    auto [ptr, ec] =
        std::from_chars(val.data(), val.data() + val.size(), remaining);
    if (ec == std::errc{} && ptr == val.data() + val.size()) {
      rate_limiter_.update((std::max)(remaining, int64_t{0}));
    }
  }
}

std::expected<OrderAck, OrderError>
AlpacaRestClient::placeOrder(const Order &order) {
  if (!rate_limiter_.waitOrDrop()) {
    return std::unexpected(OrderError{429, "Rate limited (dropped by policy)"});
  }
  buildOrderPayload(order);

  session_->SetUrl(cpr::Url{order_url_});
  session_->SetBody(cpr::Body{payload_buf_});
  const cpr::Response r = session_->Post();
  reapplyQuickAck();
  updateRateLimit(r);

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
  if (!rate_limiter_.waitOrDrop()) {
    return std::unexpected(OrderError{429, "Rate limited (dropped by policy)"});
  }
  session_->SetUrl(cpr::Url{cancel_url_prefix_ + std::string(alpaca_order_id)});
  session_->RemoveContent();
  const cpr::Response r = session_->Delete();
  reapplyQuickAck();
  updateRateLimit(r);

  if (r.status_code == 204) {
    return {};
  }

  return std::unexpected(
      OrderError{r.status_code, "Error canceling order: " + r.text});
}

std::expected<std::vector<AlpacaOrderStatus>, OrderError>
AlpacaRestClient::queryOrders(std::string_view status_filter,
                              std::string_view after) {
  if (!rate_limiter_.waitOrDrop()) {
    return std::unexpected(OrderError{429, "Rate limited (dropped by policy)"});
  }

  cpr::Parameters params{{"status", std::string(status_filter)},
                         {"limit", "500"},
                         {"direction", after.empty() ? "desc" : "asc"}};
  if (!after.empty()) {
    params.Add({"after", std::string(after)});
  }
  session_->SetUrl(cpr::Url{order_url_});
  session_->SetParameters(params);
  session_->RemoveContent();
  const cpr::Response r = session_->Get();
  reapplyQuickAck();
  updateRateLimit(r);

  if (r.status_code >= 400) {
    return std::unexpected(
        OrderError{r.status_code, "Error querying orders: " + r.text});
  }

  auto response = nlohmann::json::parse(r.text, nullptr, false);
  if (!response.is_array()) {
    return std::unexpected(
        OrderError{r.status_code, "Invalid JSON array in response"});
  }

  std::vector<AlpacaOrderStatus> orders;
  orders.reserve(response.size());
  for (const auto &item : response) {
    AlpacaOrderStatus order;
    order.id = item.value("id", "");
    order.symbol = item.value("symbol", "");
    order.side = item.value("side", "");
    order.status = item.value("status", "");

    const auto qty_str = item.value("qty", "0");
    const auto filled_str = item.value("filled_qty", "0");
    {
      const auto *end = qty_str.data() + qty_str.size();
      auto [ptr, ec] = std::from_chars(qty_str.data(), end, order.qty);
      if (ec != std::errc{} || ptr != end) {
        std::cerr << "AlpacaRestClient: Non-integer qty '" << qty_str
                  << "' for order " << order.id << ", skipping" << '\n';
        continue;
      }
    }
    {
      const auto *end = filled_str.data() + filled_str.size();
      auto [ptr, ec] =
          std::from_chars(filled_str.data(), end, order.filled_qty);
      if (ec != std::errc{} || ptr != end) {
        std::cerr << "AlpacaRestClient: Non-integer filled_qty '" << filled_str
                  << "' for order " << order.id << ", skipping" << '\n';
        continue;
      }
    }

    if (item.contains("filled_avg_price")) {
      const auto &price_val = item["filled_avg_price"];
      if (price_val.is_number()) {
        order.filled_avg_price = price_val.get<double>();
      } else if (price_val.is_string()) {
        const auto &s = price_val.get_ref<const std::string &>();
        if (!s.empty() && s != "null") {
          std::size_t pos = 0;
          try {
            order.filled_avg_price = std::stod(s, &pos);
            if (pos != s.size()) {
              std::cerr << "AlpacaRestClient: Trailing chars in "
                           "filled_avg_price '"
                        << s << "' for order " << order.id << '\n';
              order.filled_avg_price = 0.0;
            }
          } catch (const std::exception &e) {
            std::cerr << "AlpacaRestClient: Failed to parse filled_avg_price '"
                      << s << "': " << e.what() << '\n';
          }
        }
      }
    }

    order.created_at = item.value("created_at", "");
    orders.push_back(std::move(order));
  }

  return orders;
}

void AlpacaRestClient::reapplyQuickAck() {
#if defined(__linux__)
  reapplyTcpQuickAck(session_->GetCurlHolder()->handle);
#endif
}

const RateLimiter &AlpacaRestClient::rateLimiter() const {
  return rate_limiter_;
}
