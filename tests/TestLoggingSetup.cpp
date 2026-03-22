#include "Logging.hpp"
#include <gtest/gtest.h>
#include <spdlog/sinks/null_sink.h>

namespace {

class LoggingEnvironment : public ::testing::Environment {
public:
  void SetUp() override {
    logging::init(std::make_shared<spdlog::sinks::null_sink_mt>());
  }
  void TearDown() override { logging::shutdown(); }
};

// NOLINTNEXTLINE(cert-err58-cpp)
auto *const kEnv =
    ::testing::AddGlobalTestEnvironment(new LoggingEnvironment());

} // namespace
