#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using SocketType = SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketType = int;
#endif

#include "AlpacaRestClient.hpp"
#include <Utils.hpp>
#include <cstring>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

class AlpacaRestClientTestBase : public ::testing::Test {
protected:
  void SetUp() override {
    const char *key = std::getenv("APCA_API_KEY_ID");
    const char *secret = std::getenv("APCA_API_SECRET_KEY");
    const char *url = std::getenv("APCA_API_BASE_URL");
    if (key)
      original_key_ = key;
    if (secret)
      original_secret_ = secret;
    if (url)
      original_url_ = url;

    setenv("APCA_API_KEY_ID", "test_key", 1);
    setenv("APCA_API_SECRET_KEY", "test_secret", 1);
  }

  void TearDown() override {
    restore("APCA_API_KEY_ID", original_key_);
    restore("APCA_API_SECRET_KEY", original_secret_);
    restore("APCA_API_BASE_URL", original_url_);
  }

  void point_client_to(int port) {
    std::string url = "http://127.0.0.1:" + std::to_string(port);
    setenv("APCA_API_BASE_URL", url.c_str(), 1);
  }

  static void restore(const char *name, const std::string &val) {
    if (!val.empty())
      setenv(name, val.c_str(), 1);
    else
      unsetenv(name);
  }

private:
  std::string original_key_;
  std::string original_secret_;
  std::string original_url_;
};

class AlpacaRestClientTest : public AlpacaRestClientTestBase {};

TEST_F(AlpacaRestClientTest, ThrowsWhenApiKeyMissing) {
  unsetenv("APCA_API_KEY_ID");
  EXPECT_THROW(AlpacaRestClient(), std::runtime_error);
}

TEST_F(AlpacaRestClientTest, ThrowsWhenApiSecretMissing) {
  unsetenv("APCA_API_SECRET_KEY");
  EXPECT_THROW(AlpacaRestClient(), std::runtime_error);
}

TEST_F(AlpacaRestClientTest, UsesCustomBaseUrlWhenProvided) {
  setenv("APCA_API_BASE_URL", "https://custom.alpaca.test", 1);
  EXPECT_NO_THROW(AlpacaRestClient client);
  unsetenv("APCA_API_BASE_URL");
}

namespace {

inline void close_socket(SocketType sock) {
#ifdef _WIN32
  closesocket(sock);
#else
  close(sock);
#endif
}

struct CapturedRequest {
  std::string method;
  std::string path;
  std::string body;
};

class StubHttpServer {
public:
  explicit StubHttpServer(int status_code,
                          std::string response_body = R"({"id":"ord-123"})")
      : status_code_(status_code), response_body_(std::move(response_body)),
        server_fd_(initSocket()), port_(0) {
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&opt), sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    // NOLINTNEXTLINE(bugprone-unused-return-value,clang-analyzer-unix.StdCLibraryFunctions)
    bind(server_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    listen(server_fd_, 1);
    socklen_t len = sizeof(addr);
    getsockname(server_fd_, reinterpret_cast<sockaddr *>(&addr), &len);
    port_ = ntohs(addr.sin_port);
  }

  ~StubHttpServer() {
    close_socket(server_fd_);
#ifdef _WIN32
    WSACleanup();
#endif
  }

  StubHttpServer(const StubHttpServer &) = delete;
  StubHttpServer &operator=(const StubHttpServer &) = delete;

  [[nodiscard]] int port() const { return port_; }

  CapturedRequest serve_one() {
    SocketType client = accept(server_fd_, nullptr, nullptr);
#ifdef _WIN32
    DWORD timeout_ms = 5000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char *>(&timeout_ms), sizeof(timeout_ms));
#else
    timeval tv{};
    tv.tv_sec = 5;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    char buf[4096]{};
    int total = 0;
    while (total < 4095) {
      // NOLINTNEXTLINE(clang-analyzer-unix.StdCLibraryFunctions)
      auto n = recv(client, buf + total, 4095 - total, 0);
      if (n <= 0)
        break;
      total += static_cast<int>(n);
      std::string_view sv(buf, total);
      auto hdr_end = sv.find("\r\n\r\n");
      if (hdr_end == std::string_view::npos)
        continue;
      auto cl = sv.find("Content-Length: ");
      if (cl == std::string_view::npos)
        break;
      auto cl_end = sv.find("\r\n", cl);
      int body_len =
          std::stoi(std::string(sv.substr(cl + 16, cl_end - cl - 16)));
      if (total >= static_cast<int>(hdr_end + 4) + body_len)
        break;
    }

    std::string raw(buf, total);
    std::string response =
        "HTTP/1.1 " + std::to_string(status_code_) +
        " \r\nContent-Length: " + std::to_string(response_body_.size()) +
        "\r\n\r\n" + response_body_;
    send(client, response.c_str(), static_cast<int>(response.size()), 0);
    close_socket(client);

    CapturedRequest req;
    auto first_space = raw.find(' ');
    if (first_space != std::string::npos)
      req.method = raw.substr(0, first_space);
    auto second_space = raw.find(' ', first_space + 1);
    if (first_space != std::string::npos && second_space != std::string::npos)
      req.path = raw.substr(first_space + 1, second_space - first_space - 1);
    auto body_start = raw.find("\r\n\r\n");
    if (body_start != std::string::npos)
      req.body = raw.substr(body_start + 4);
    return req;
  }

private:
  static SocketType initSocket() {
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    return socket(AF_INET, SOCK_STREAM, 0);
  }

  int status_code_;
  std::string response_body_;
  SocketType server_fd_;
  int port_;
};

Order make_order(const char *symbol, OrderSide side, OrderType type,
                 int64_t qty, uint64_t price) {
  Order order{};
  std::memset(order.symbol, 0, sizeof(order.symbol));
  auto len = (std::min)(std::strlen(symbol), sizeof(order.symbol));
  std::memcpy(order.symbol, symbol, len);
  order.side = side;
  order.type = type;
  order.quantity = qty;
  order.price = price;
  return order;
}

} // namespace

class AlpacaRestClientPlaceOrderTest : public AlpacaRestClientTestBase {};

struct MarketOrderParam {
  const char *symbol;
  OrderSide side;
  int64_t qty;
  const char *expected_side;
};

class AlpacaMarketOrderTest
    : public AlpacaRestClientTestBase,
      public ::testing::WithParamInterface<MarketOrderParam> {};

TEST_P(AlpacaMarketOrderTest, PayloadIsCorrect) {
  const auto &[symbol, side, qty, expected_side] = GetParam();
  StubHttpServer server(200);
  point_client_to(server.port());
  AlpacaRestClient client;
  Order order = make_order(symbol, side, OrderType::Market, qty, 0);

  CapturedRequest req;
  std::thread t([&]() { req = server.serve_one(); });
  auto result = client.placeOrder(order);
  t.join();

  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(req.path, "/v2/orders");
  auto json = nlohmann::json::parse(req.body);
  EXPECT_EQ(json["symbol"], symbol);
  EXPECT_EQ(json["qty"], std::to_string(qty));
  EXPECT_EQ(json["side"], expected_side);
  EXPECT_EQ(json["type"], "market");
  EXPECT_EQ(json["time_in_force"], "day");
  EXPECT_FALSE(json.contains("limit_price"));
}

INSTANTIATE_TEST_SUITE_P(
    BuySell, AlpacaMarketOrderTest,
    ::testing::Values(MarketOrderParam{"AAPL", OrderSide::Buy, 100, "buy"},
                      MarketOrderParam{"TSLA", OrderSide::Sell, 50, "sell"}));

struct LimitOrderParam {
  const char *symbol;
  OrderSide side;
  int64_t qty;
  uint64_t price;
  const char *expected_side;
  double expected_limit_price;
};

class AlpacaLimitOrderTest
    : public AlpacaRestClientTestBase,
      public ::testing::WithParamInterface<LimitOrderParam> {};

TEST_P(AlpacaLimitOrderTest, PayloadIsCorrect) {
  const auto &[symbol, side, qty, price, expected_side, expected_limit_price] =
      GetParam();
  StubHttpServer server(200);
  point_client_to(server.port());
  AlpacaRestClient client;
  Order order = make_order(symbol, side, OrderType::Limit, qty, price);

  CapturedRequest req;
  std::thread t([&]() { req = server.serve_one(); });
  auto result = client.placeOrder(order);
  t.join();

  EXPECT_TRUE(result.has_value());
  auto json = nlohmann::json::parse(req.body);
  EXPECT_EQ(json["side"], expected_side);
  EXPECT_EQ(json["type"], "limit");
  EXPECT_TRUE(json.contains("limit_price"));
  double limit_price = std::stod(json["limit_price"].get<std::string>());
  EXPECT_DOUBLE_EQ(limit_price, expected_limit_price);
}

INSTANTIATE_TEST_SUITE_P(
    BuySell, AlpacaLimitOrderTest,
    ::testing::Values(
        LimitOrderParam{"MSFT", OrderSide::Buy, 10, 4500000, "buy", 450.0},
        LimitOrderParam{"GOOG", OrderSide::Sell, 5, 1750000, "sell", 175.0}));

TEST_F(AlpacaRestClientPlaceOrderTest, ErrorResponseReturnsUnexpected) {
  StubHttpServer server(422, R"({"message":"insufficient qty"})");
  point_client_to(server.port());
  AlpacaRestClient client;
  Order order = make_order("AAPL", OrderSide::Buy, OrderType::Market, 100, 0);

  CapturedRequest req;
  std::thread t([&]() { req = server.serve_one(); });
  auto result = client.placeOrder(order);
  t.join();

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().status_code, 422);
  EXPECT_TRUE(result.error().message.find("insufficient qty") !=
              std::string::npos);
}

TEST_F(AlpacaRestClientPlaceOrderTest, SuccessResponseReturnsOrderAck) {
  StubHttpServer server(200, R"({"id":"ord-abc-123","status":"accepted"})");
  point_client_to(server.port());
  AlpacaRestClient client;
  Order order = make_order("AAPL", OrderSide::Buy, OrderType::Market, 100, 0);

  CapturedRequest req;
  std::thread t([&]() { req = server.serve_one(); });
  auto result = client.placeOrder(order);
  t.join();

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->client_order_id, "ord-abc-123");
  EXPECT_EQ(result->status, "accepted");
}

class AlpacaRestClientCancelOrderTest : public AlpacaRestClientTestBase {};

TEST_F(AlpacaRestClientCancelOrderTest, Cancel204ReturnsSuccess) {
  StubHttpServer server(204, "");
  point_client_to(server.port());
  AlpacaRestClient client;

  CapturedRequest req;
  std::thread t([&]() { req = server.serve_one(); });
  auto result = client.cancelOrder("order-uuid-123");
  t.join();

  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(req.method, "DELETE");
  EXPECT_EQ(req.path, "/v2/orders/order-uuid-123");
}

TEST_F(AlpacaRestClientCancelOrderTest, Cancel404ReturnsError) {
  StubHttpServer server(404, R"({"message":"order not found"})");
  point_client_to(server.port());
  AlpacaRestClient client;

  CapturedRequest req;
  std::thread t([&]() { req = server.serve_one(); });
  auto result = client.cancelOrder("gone-order-id");
  t.join();

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().status_code, 404);
}

TEST_F(AlpacaRestClientCancelOrderTest, Cancel422ReturnsError) {
  StubHttpServer server(422, R"({"message":"order already filled"})");
  point_client_to(server.port());
  AlpacaRestClient client;

  CapturedRequest req;
  std::thread t([&]() { req = server.serve_one(); });
  auto result = client.cancelOrder("filled-order-id");
  t.join();

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().status_code, 422);
  EXPECT_TRUE(result.error().message.find("order already filled") !=
              std::string::npos);
}
