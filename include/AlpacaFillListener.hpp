#pragma once

#include "IFillListener.hpp"
#include "PositionManager.hpp"
#include <atomic>
#include <ixwebsocket/IXWebSocket.h>
#include <memory>
#include <string>
#include <variant>

struct FillEvent {
  char symbol[8];
  OrderSide side;
  int64_t quantity;
  double price;
};

struct CancelEvent {
  char symbol[8];
  OrderSide side;
  int64_t quantity;
};

using TradeUpdate = std::variant<std::monostate, FillEvent, CancelEvent>;

[[nodiscard]] TradeUpdate parseTradingUpdate(const std::string &json);

class AlpacaFillListener : public IFillListener {
public:
  AlpacaFillListener(std::shared_ptr<PositionManager> position_manager,
                     std::atomic<bool> &is_running, const std::string &api_key,
                     const std::string &api_secret);

  void start() override;
  void stop() override;

private:
  void onMessage(const ix::WebSocketMessagePtr &msg);
  void sendAuth();
  void sendSubscribe();
  void handleTradeUpdate(const std::string &json);

  std::shared_ptr<PositionManager> position_manager_;
  std::atomic<bool> &is_running_;
  std::string api_key_;
  std::string api_secret_;
  ix::WebSocket ws_;
};
