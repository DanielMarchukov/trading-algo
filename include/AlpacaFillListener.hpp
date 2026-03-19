#pragma once

#include "IFillListener.hpp"
#include "PositionManager.hpp"
#include <atomic>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>

struct FillEvent {
  char symbol[8];
  OrderSide side;
  bool is_partial;
  int64_t quantity;
  double price;
  char alpaca_order_id[48];
};

static_assert(sizeof(FillEvent) == 80, "FillEvent must be 80 bytes");
static_assert(alignof(FillEvent) == 8, "FillEvent must be 8-byte aligned");
static_assert(std::is_trivially_copyable_v<FillEvent>,
              "FillEvent must be trivially copyable");

struct CancelEvent {
  char symbol[8];
  OrderSide side;
  int64_t quantity;
  char alpaca_order_id[48];
};

static_assert(sizeof(CancelEvent) == 72, "CancelEvent must be 72 bytes");
static_assert(alignof(CancelEvent) == 8, "CancelEvent must be 8-byte aligned");
static_assert(std::is_trivially_copyable_v<CancelEvent>,
              "CancelEvent must be trivially copyable");

using TradeUpdate = std::variant<std::monostate, FillEvent, CancelEvent>;

[[nodiscard]] TradeUpdate parseTradingUpdate(const nlohmann::json &parsed);

class IRestClient;
class LatencyTracker;
class PendingOrderTracker;

class AlpacaFillListener : public IFillListener {
  friend class FillListenerTest;
  friend class FillListenerTrackerTest;
  friend class FillListenerReconcileTest;

public:
  AlpacaFillListener(PositionManager *position_manager,
                     std::atomic<bool> &is_running, const std::string &api_key,
                     const std::string &api_secret,
                     PendingOrderTracker *pending_tracker = nullptr,
                     LatencyTracker *latency_tracker = nullptr,
                     IRestClient *reconciliation_client = nullptr);

  ~AlpacaFillListener() override;

  void start() override;
  void stop() override;

private:
  void onMessage(const ix::WebSocketMessagePtr &msg);
  void sendAuth();
  void sendSubscribe();
  void handleTradeUpdate(const std::string &json);
  void reconcileAfterReconnect();

  PositionManager *position_manager_;
  PendingOrderTracker *pending_tracker_;
  LatencyTracker *latency_tracker_;
  IRestClient *reconciliation_client_;
  std::atomic<bool> &is_running_;
  std::string api_key_;
  std::string api_secret_;
  ix::WebSocket ws_;
  std::thread thread_;
  bool has_connected_{false};
  std::unordered_map<std::string, int64_t> filled_qty_by_order_;
  std::unordered_set<std::string> completed_order_ids_;
};
