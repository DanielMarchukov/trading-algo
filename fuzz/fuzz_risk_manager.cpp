#include "Logging.hpp"
#include "Order.hpp"
#include "PositionManager.hpp"
#include "RiskManager.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <spdlog/sinks/null_sink.h>

namespace {

struct FuzzInit {
  FuzzInit() { logging::init(std::make_shared<spdlog::sinks::null_sink_mt>()); }
};

// NOLINTNEXTLINE(cert-err58-cpp)
[[maybe_unused]] FuzzInit kInit;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < sizeof(Order)) {
    return 0;
  }

  alignas(64) Order order{};
  std::memcpy(&order, data, sizeof(Order));

  PositionManager pm;
  RiskManager rm(&pm);
  (void)rm.onNewOrder(order);

  return 0;
}
