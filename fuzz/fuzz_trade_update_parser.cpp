#include "AlpacaFillListener.hpp"
#include "Logging.hpp"
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/null_sink.h>
#include <string>
#include <variant>

namespace {

struct FuzzInit {
  FuzzInit() { logging::init(std::make_shared<spdlog::sinks::null_sink_mt>()); }
};

// NOLINTNEXTLINE(cert-err58-cpp)
[[maybe_unused]] FuzzInit kInit;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  const std::string json_str(reinterpret_cast<const char *>(data), size);

  nlohmann::json parsed;
  try {
    parsed = nlohmann::json::parse(json_str);
  } catch (const nlohmann::json::exception &) {
    return 0;
  }

  const TradeUpdate update = parseTradingUpdate(parsed);

  if (std::holds_alternative<FillEvent>(update)) {
    const auto &fill = std::get<FillEvent>(update);
    (void)fill.quantity;
    (void)fill.price;
  } else if (std::holds_alternative<CancelEvent>(update)) {
    const auto &cancel = std::get<CancelEvent>(update);
    (void)cancel.quantity;
  }

  return 0;
}
