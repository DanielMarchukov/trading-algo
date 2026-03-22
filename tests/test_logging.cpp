#include "Logging.hpp"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

class LoggingTest : public ::testing::Test {
protected:
  void SetUp() override { spdlog::drop_all(); }

  void TearDown() override {
    spdlog::drop_all();
    logging::initForTests();
  }
};

struct LoggingInitParam {
  const char *name;
  void (*init_fn)();
  void (*cleanup_fn)();
};

class LoggingInitTest : public LoggingTest,
                        public ::testing::WithParamInterface<LoggingInitParam> {
};

TEST_P(LoggingInitTest, RegistersAllNamedLoggers) {
  const auto &param = GetParam();
  param.init_fn();

  EXPECT_NE(spdlog::get("engine"), nullptr);
  EXPECT_NE(spdlog::get("gateway"), nullptr);
  EXPECT_NE(spdlog::get("fills"), nullptr);
  EXPECT_NE(spdlog::get("rest"), nullptr);
  EXPECT_NE(spdlog::get("market"), nullptr);
  EXPECT_NE(spdlog::get("zmq"), nullptr);
  EXPECT_NE(spdlog::get("system"), nullptr);
  EXPECT_NE(spdlog::get("latency"), nullptr);

  if (param.cleanup_fn) {
    param.cleanup_fn();
  }
}

TEST_P(LoggingInitTest, SetsDefaultLoggerToSystem) {
  const auto &param = GetParam();
  param.init_fn();

  EXPECT_EQ(spdlog::default_logger()->name(), "system");

  if (param.cleanup_fn) {
    param.cleanup_fn();
  }
}

INSTANTIATE_TEST_SUITE_P(
    InitVariants, LoggingInitTest,
    ::testing::Values(
        LoggingInitParam{"init", logging::init, logging::shutdown},
        LoggingInitParam{"initForTests", logging::initForTests, nullptr}),
    [](const auto &info) { return info.param.name; });

TEST_F(LoggingTest, InitForTestsSkipsAlreadyRegisteredLoggers) {
  logging::initForTests();
  auto original = spdlog::get("gateway");

  logging::initForTests();
  EXPECT_EQ(spdlog::get("gateway"), original);
}

TEST_F(LoggingTest, ShutdownDropsAllLoggers) {
  logging::initForTests();
  ASSERT_NE(spdlog::get("engine"), nullptr);

  logging::shutdown();
  EXPECT_EQ(spdlog::get("engine"), nullptr);
}
