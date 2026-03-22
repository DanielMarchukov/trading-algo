#include "AlpacaMsgpackDecoder.hpp"
#include "Logging.hpp"
#include "MarketEvent.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <spdlog/sinks/null_sink.h>
#include <string_view>

namespace {

struct FuzzInit {
  FuzzInit() { logging::init(std::make_shared<spdlog::sinks::null_sink_mt>()); }
};

// NOLINTNEXTLINE(cert-err58-cpp)
[[maybe_unused]] FuzzInit kInit;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  AlpacaMsgpackDecoder decoder;
  decoder.setOnAuthSuccess([]() {});

  decoder.decode(
      std::span<const char>(reinterpret_cast<const char *>(data), size),
      12345ULL,
      [](const MarketEvent & /*event*/, std::string_view /*symbol*/) {});

  return 0;
}
