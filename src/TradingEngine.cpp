#include "TradingEngine.hpp"
#include "AlpacaFillListener.hpp"
#include "ThreadPinning.hpp"
#include "Utils.hpp"
#include <csignal>
#include <cstdlib>
#include <iostream>

namespace {
std::atomic<bool> *g_is_running_ptr = nullptr;
void signalHandler(const int signum) {
  (void)signum;
  if (g_is_running_ptr) {
    g_is_running_ptr->store(false);
  }
}
} // namespace

TradingEngine::TradingEngine(const std::vector<std::string> &symbols,
                             std::unique_ptr<IRestClient> rest_client)
    : is_running_(true), symbols_(symbols) {
  setup_signal_handler();
  latency_tracker_ = std::make_unique<LatencyTracker>();
  position_manager_ = std::make_unique<PositionManager>();
  for (const auto &symbol : symbols_) {
    position_manager_->registerSymbol(symbol);
  }
  order_cooldown_ = std::make_unique<OrderCooldown>();
  for (const auto &symbol : symbols_) {
    order_cooldown_->registerSymbol(symbol);
  }
  risk_manager_ = std::make_unique<RiskManager>(position_manager_.get(),
                                                order_cooldown_.get());
  pending_tracker_ = std::make_unique<PendingOrderTracker>();
  order_queue_ = std::make_unique<LockFreeMPSCQueue<Order>>();
  order_gateway_ = std::make_unique<OrderGateway>(
      is_running_, order_queue_.get(), std::move(rest_client),
      position_manager_.get(), pending_tracker_.get(), latency_tracker_.get());

  const char *api_key = std::getenv("APCA_API_KEY_ID");
  const char *api_secret = std::getenv("APCA_API_SECRET_KEY");
  if (!api_key || !api_secret) {
    throw std::runtime_error(
        "APCA_API_KEY_ID and APCA_API_SECRET_KEY must be set");
  }
  fill_listener_ = std::make_unique<AlpacaFillListener>(
      position_manager_.get(), is_running_, api_key, api_secret,
      pending_tracker_.get(), latency_tracker_.get());

  ipc_address_ = getZmqMarketDataAddress();

  auto source =
      AlpacaWebSocketSource(api_key, api_secret, symbols_, is_running_);
  auto decoder = AlpacaMsgpackDecoder();
  auto sink = ZmqMarketEventSink(context_, ipc_address_);
  market_publisher_ = std::make_unique<AlpacaPipeline>(
      std::move(source), std::move(decoder), std::move(sink));
}

TradingEngine::~TradingEngine() {
  std::cout << "TradingEngine destructor: Starting shutdown..." << '\n';
  if (is_running_.load()) {
    stop();
  }
  shutdown();
  g_is_running_ptr = nullptr;
  std::cout << "TradingEngine destructor: Shutdown complete." << '\n';
}

void TradingEngine::setup_signal_handler() {
  g_is_running_ptr = &is_running_;
  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);
}

void TradingEngine::launch_gateway() {
  order_gateway_thread_ = std::thread(&OrderGateway::run, order_gateway_.get());
  pin_thread_to_core(order_gateway_thread_, 0);
  std::cout << "Pinned OrderGateway thread to CPU Core 0" << '\n';
}

void TradingEngine::launch_consumers() {
  const uint32_t max_cores = std::thread::hardware_concurrency();
  if (max_cores > 0 && symbols_.size() + 2 > max_cores) {
    std::cerr << "Warning: " << symbols_.size()
              << " symbols + 2 reserved cores exceeds " << max_cores
              << " available cores; thread pinning will wrap" << '\n';
  }

  for (uint32_t i = 0; i < symbols_.size(); ++i) {
    const auto &symbol = symbols_[i];
    OrderQueuePusher callback{order_queue_.get()};

    auto consumer = std::make_unique<ConsumerType>(
        context_, ipc_address_, symbol, is_running_, callback,
        risk_manager_.get(), latency_tracker_.get());

    consumer_threads_.push_back(
        {std::thread(&ConsumerType::run, consumer.get()), std::move(consumer)});

    uint32_t core_id = max_cores > 0 ? (i + 2) % max_cores : i + 2;
    pin_thread_to_core(consumer_threads_.back().thread, core_id);
    std::cout << "Pinned thread for " << symbol << " to CPU Core " << core_id
              << '\n';
  }
}

void TradingEngine::main_loop() const {
  while (is_running_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void TradingEngine::shutdown() {
  if (shutdown_done_.exchange(true)) {
    return;
  }

  market_publisher_->stop();

  for (auto &[thread, consumer] : consumer_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  consumer_threads_.clear();

  fill_listener_->stop();

  if (order_gateway_thread_.joinable()) {
    order_gateway_thread_.join();
  }
  order_gateway_.reset();

  if (latency_tracker_) {
    try {
      latency_tracker_->dump(".");
    } catch (const std::exception &e) {
      std::cerr << "Latency tracker dump failed: " << e.what() << '\n';
    } catch (...) {
      std::cerr << "Latency tracker dump failed with unknown error" << '\n';
    }
  }
}

void TradingEngine::run() {
  std::cout << "Starting trading engine..." << '\n';
  fill_listener_->start();
  std::cout << "FillListener started" << '\n';
  market_publisher_->start();
  std::cout << "MarketPublisher started" << '\n';
  launch_gateway();
  launch_consumers();
  main_loop();

  std::cout << "Main loop exited. Shutting down threads..." << '\n';
  shutdown();
}

void TradingEngine::stop() { is_running_.store(false); }
