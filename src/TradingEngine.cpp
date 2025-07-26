#include "TradingEngine.hpp"
#include <csignal>
#include <filesystem>
#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__) || defined(__gnu_linux__)
#include <pthread.h>
#elif defined(__APPLE__)
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#endif

namespace {
std::atomic<bool> *g_is_running_ptr = nullptr;
void signal_handler(const int signum) {
  if (g_is_running_ptr) {
    std::cout << "\nSignal " << signum << " received. Initiating shutdown..."
              << std::endl;
    g_is_running_ptr->store(false);
  }
}
} // namespace

void pin_thread_to_core(std::thread &t, size_t core_id) {
#if defined(__linux__) || defined(__gnu_linux__)
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);
  if (pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset) !=
      0) {
    std::cerr << "Error calling pthread_setaffinity_np\n";
  }
#elif defined(_WIN32)
  if (const DWORD_PTR mask = 1LL << core_id;
      SetThreadAffinityMask(t.native_handle(), mask) == 0) {
    std::cerr << "Error calling SetThreadAffinityMask: " << GetLastError()
              << std::endl;
  }
#elif defined(__APPLE__)
  thread_affinity_policy_data_t policy = {static_cast<integer_t>(core_id)};
  thread_port_t mach_thread = pthread_mach_thread_np(t.native_handle());
  if (thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY,
                        (thread_policy_t)&policy,
                        THREAD_AFFINITY_POLICY_COUNT) != KERN_SUCCESS) {
    std::cerr << "Error calling thread_policy_set" << std::endl;
  }
#else
  (void)t;
  (void)core_id;
  std::cout << "Warning: CPU pinning not supported on this platform."
            << std::endl;
#endif
}

TradingEngine::TradingEngine(const std::vector<std::string> &symbols,
                             std::unique_ptr<IRestClient> rest_client)
    : is_running_(true), symbols_(symbols) {
  setup_signal_handler();
  risk_manager_ =
      std::make_shared<RiskManager>(std::make_shared<PositionManager>());
  order_queue_ = std::make_shared<ThreadSafeQueue<Order>>();
  order_gateway_ = std::make_unique<OrderGateway>(is_running_, order_queue_,
                                                  std::move(rest_client));

#ifdef _WIN32
  // Windows doesn't support IPC, so production must use TCP.
  ipc_address_ = "tcp://127.0.0.1:5555";
#else
  std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
  std::filesystem::path socket_path = temp_dir / "market_data.sock";
  ipc_address_ = "ipc://" + socket_path.string();
#endif
}

TradingEngine::~TradingEngine() {
  std::cout << "TradingEngine destructor: Starting shutdown..." << std::endl;
  if (is_running_.load()) {
    stop();
  }
  shutdown();
  std::cout << "TradingEngine destructor: Shutdown complete." << std::endl;
}

void TradingEngine::setup_signal_handler() {
  g_is_running_ptr = &is_running_;
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
}

void TradingEngine::launch_gateway() {
  order_gateway_thread_ = std::thread(&OrderGateway::run, order_gateway_.get());
  std::cout << "Pinned OrderGateway thread to CPU Core 1" << std::endl;
  pin_thread_to_core(order_gateway_thread_, 1);
}

void TradingEngine::launch_consumers() {
  for (size_t i = 0; i < symbols_.size(); ++i) {
    const auto &symbol = symbols_[i];
    auto callback = [this](const Order &order) {
      this->order_queue_->push(order);
    };

    auto consumer =
        std::make_unique<MarketEventConsumer<SimpleMarketMakingStrategy>>(
            context_, ipc_address_, symbol, is_running_, callback,
            risk_manager_);

    consumer_threads_.push_back(
        {std::thread(&MarketEventConsumer<SimpleMarketMakingStrategy>::run,
                     consumer.get()),
         std::move(consumer)});

    pin_thread_to_core(consumer_threads_.back().thread, i + 2);
    std::cout << "Pinned thread for " << symbol << " to CPU Core " << (i + 2)
              << std::endl;
  }
}

void TradingEngine::main_loop() const {
  while (is_running_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void TradingEngine::shutdown() {
  if (order_gateway_thread_.joinable()) {
    order_gateway_thread_.join();
  }
  for (auto &[thread, consumer] : consumer_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  consumer_threads_.clear();
  order_gateway_.reset();
}

void TradingEngine::run() {
  std::cout << "Starting trading engine..." << std::endl;
  launch_gateway();
  launch_consumers();
  main_loop();

  std::cout << "Main loop exited. Shutting down threads..." << std::endl;
  shutdown();
}

void TradingEngine::stop() { is_running_.store(false); }
