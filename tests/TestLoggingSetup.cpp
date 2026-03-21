#include "Logging.hpp"
#include <gtest/gtest.h>

namespace {

class LoggingEnvironment : public ::testing::Environment {
public:
  void SetUp() override { logging::initForTests(); }
  void TearDown() override { logging::shutdown(); }
};

// Register before RUN_ALL_TESTS is called by gtest_main.
// NOLINTNEXTLINE(cert-err58-cpp)
auto *const kEnv =
    ::testing::AddGlobalTestEnvironment(new LoggingEnvironment());

} // namespace
