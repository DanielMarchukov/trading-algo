#include "Logging.hpp"
#include <array>
#include <spdlog/async.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace {

constexpr std::array kLoggerNames = {"engine", "gateway", "fills",  "rest",
                                     "market", "zmq",     "system", "latency"};

} // namespace

namespace logging {

void init() {
  spdlog::init_thread_pool(8192, 1);

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");

  for (const auto *name : kLoggerNames) {
    auto logger = std::make_shared<spdlog::async_logger>(
        name, console_sink, spdlog::thread_pool(),
        spdlog::async_overflow_policy::overrun_oldest);
    logger->set_level(spdlog::level::trace);
    spdlog::register_logger(logger);
  }

  spdlog::set_default_logger(spdlog::get("system"));
}

void initForTests() {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();

  for (const auto *name : kLoggerNames) {
    if (spdlog::get(name)) {
      continue;
    }
    auto logger = std::make_shared<spdlog::logger>(name, null_sink);
    logger->set_level(spdlog::level::trace);
    spdlog::register_logger(logger);
  }

  spdlog::set_default_logger(spdlog::get("system"));
}

void shutdown() { spdlog::shutdown(); }

} // namespace logging
