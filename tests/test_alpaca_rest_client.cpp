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

class AlpacaRestClientTest : public ::testing::Test {
protected:
  void SetUp() override {
    const char *key = std::getenv("APCA_API_KEY_ID");
    const char *secret = std::getenv("APCA_API_SECRET_KEY");
    if (key)
      original_key_ = key;
    if (secret)
      original_secret_ = secret;

    setenv("APCA_API_KEY_ID", "test_key", 1);
    setenv("APCA_API_SECRET_KEY", "test_secret", 1);
  }

  void TearDown() override {
    if (!original_key_.empty()) {
      setenv("APCA_API_KEY_ID", original_key_.c_str(), 1);
    } else {
      unsetenv("APCA_API_KEY_ID");
    }

    if (!original_secret_.empty()) {
      setenv("APCA_API_SECRET_KEY", original_secret_.c_str(), 1);
    } else {
      unsetenv("APCA_API_SECRET_KEY");
    }
  }

private:
  std::string original_key_;
  std::string original_secret_;
};

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
  std::string path;
  std::string body;
};

class StubHttpServer {
public:
  explicit StubHttpServer(int status_code,
                          std::string response_body = R"({"id":"ord-123"})")
      : status_code_(status_code), response_body_(std::move(response_body)) {
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&opt), sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
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
               reinterpret_cast<const char *>(&timeout_ms),
               sizeof(timeout_ms));
#else
    timeval tv{};
    tv.tv_sec = 5;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    char buf[4096]{};
    int total = 0;
    while (total < 4095) {
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
    std::string response = "HTTP/1.1 " + std::to_string(status_code_) +
                           " \r\nContent-Length: " +
                           std::to_string(response_body_.size()) +
                           "\r\n\r\n" + response_body_;
    send(client, response.c_str(), static_cast<int>(response.size()), 0);
    close_socket(client);

    CapturedRequest req;
    auto first_space = raw.find(' ');
    auto second_space = raw.find(' ', first_space + 1);
    if (first_space != std::string::npos && second_space != std::string::npos)
      req.path = raw.substr(first_space + 1, second_space - first_space - 1);
    auto body_start = raw.find("\r\n\r\n");
    if (body_start != std::string::npos)
      req.body = raw.substr(body_start + 4);
    return req;
  }

private:
  SocketType server_fd_;
  int port_;
  int status_code_;
  std::string response_body_;
};

Order make_order(const char *symbol, OrderSide side, OrderType type,
                 int64_t qty, uint64_t price) {
  Order order{};
  std::memset(order.symbol, 0, sizeof(order.symbol));
  auto len = std::min(std::strlen(symbol), sizeof(order.symbol));
  std::memcpy(order.symbol, symbol, len);
  order.side = side;
  order.type = type;
  order.quantity = qty;
  order.price = price;
  return order;
}

} // namespace

class AlpacaRestClientPlaceOrderTest : public ::testing::Test {
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

TEST_F(AlpacaRestClientPlaceOrderTest, MarketBuyOrderPayload) {
  StubHttpServer server(200);
  point_client_to(server.port());
  AlpacaRestClient client;
  Order order = make_order("AAPL", OrderSide::Buy, OrderType::Market, 100, 0);

  CapturedRequest req;
  std::thread t([&]() { req = server.serve_one(); });
  client.placeOrder(order);
  t.join();

  EXPECT_EQ(req.path, "/v2/orders");
  auto json = nlohmann::json::parse(req.body);
  EXPECT_EQ(json["symbol"], "AAPL");
  EXPECT_EQ(json["qty"], "100");
  EXPECT_EQ(json["side"], "buy");
  EXPECT_EQ(json["type"], "market");
  EXPECT_EQ(json["time_in_force"], "day");
  EXPECT_FALSE(json.contains("limit_price"));
}

TEST_F(AlpacaRestClientPlaceOrderTest, MarketSellOrderPayload) {
  StubHttpServer server(200);
  point_client_to(server.port());
  AlpacaRestClient client;
  Order order =
      make_order("TSLA", OrderSide::Sell, OrderType::Market, 50, 0);

  CapturedRequest req;
  std::thread t([&]() { req = server.serve_one(); });
  client.placeOrder(order);
  t.join();

  auto json = nlohmann::json::parse(req.body);
  EXPECT_EQ(json["side"], "sell");
  EXPECT_EQ(json["type"], "market");
  EXPECT_FALSE(json.contains("limit_price"));
}

TEST_F(AlpacaRestClientPlaceOrderTest, LimitBuyOrderIncludesScaledPrice) {
  StubHttpServer server(200);
  point_client_to(server.port());
  AlpacaRestClient client;
  Order order =
      make_order("MSFT", OrderSide::Buy, OrderType::Limit, 10, 4500000);

  CapturedRequest req;
  std::thread t([&]() { req = server.serve_one(); });
  client.placeOrder(order);
  t.join();

  auto json = nlohmann::json::parse(req.body);
  EXPECT_EQ(json["type"], "limit");
  EXPECT_TRUE(json.contains("limit_price"));
  double limit_price = std::stod(json["limit_price"].get<std::string>());
  EXPECT_DOUBLE_EQ(limit_price, 450.0);
}

TEST_F(AlpacaRestClientPlaceOrderTest, LimitSellOrderPayload) {
  StubHttpServer server(200);
  point_client_to(server.port());
  AlpacaRestClient client;
  Order order =
      make_order("GOOG", OrderSide::Sell, OrderType::Limit, 5, 1750000);

  CapturedRequest req;
  std::thread t([&]() { req = server.serve_one(); });
  client.placeOrder(order);
  t.join();

  auto json = nlohmann::json::parse(req.body);
  EXPECT_EQ(json["side"], "sell");
  EXPECT_EQ(json["type"], "limit");
  EXPECT_TRUE(json.contains("limit_price"));
  double limit_price = std::stod(json["limit_price"].get<std::string>());
  EXPECT_DOUBLE_EQ(limit_price, 175.0);
}

TEST_F(AlpacaRestClientPlaceOrderTest, ErrorResponsePrintsToStderr) {
  StubHttpServer server(422, R"({"message":"insufficient qty"})");
  point_client_to(server.port());
  AlpacaRestClient client;
  Order order =
      make_order("AAPL", OrderSide::Buy, OrderType::Market, 100, 0);

  std::stringstream captured;
  auto *original = std::cerr.rdbuf(captured.rdbuf());

  CapturedRequest req;
  std::thread t([&]() { req = server.serve_one(); });
  client.placeOrder(order);
  t.join();

  std::cerr.rdbuf(original);
  EXPECT_TRUE(captured.str().find("Error placing order") != std::string::npos);
  EXPECT_TRUE(captured.str().find("422") != std::string::npos);
}

TEST_F(AlpacaRestClientPlaceOrderTest, SuccessResponsePrintsToStdout) {
  StubHttpServer server(200, R"({"id":"ord-abc-123"})");
  point_client_to(server.port());
  AlpacaRestClient client;
  Order order =
      make_order("AAPL", OrderSide::Buy, OrderType::Market, 100, 0);

  std::stringstream captured;
  auto *original = std::cout.rdbuf(captured.rdbuf());

  CapturedRequest req;
  std::thread t([&]() { req = server.serve_one(); });
  client.placeOrder(order);
  t.join();

  std::cout.rdbuf(original);
  EXPECT_TRUE(captured.str().find("Successfully placed order") !=
              std::string::npos);
}
