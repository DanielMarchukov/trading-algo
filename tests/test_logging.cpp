#include "Logging.hpp"
#include <gtest/gtest.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

namespace {

auto null_sink() { return std::make_shared<spdlog::sinks::null_sink_mt>(); }

} // namespace

class LoggingTest : public ::testing::Test {
protected:
  void SetUp() override { spdlog::drop_all(); }

  void TearDown() override {
    spdlog::drop_all();
    logging::init(null_sink());
  }
};

struct LoggingInitParam {
  const char *name;
  std::function<void()> init_fn;
  std::function<void()> cleanup_fn;
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
    ::testing::Values(LoggingInitParam{"production", [] { logging::init(); },
                                       logging::shutdown},
                      LoggingInitParam{"null_sink",
                                       [] { logging::init(null_sink()); },
                                       nullptr}),
    [](const auto &info) { return info.param.name; });

TEST_F(LoggingTest, InitSkipsAlreadyRegisteredLoggers) {
  logging::init(null_sink());
  auto original = spdlog::get("gateway");

  spdlog::drop_all();
  logging::init(null_sink());
  EXPECT_NE(spdlog::get("gateway"), nullptr);
}

TEST_F(LoggingTest, ShutdownDropsAllLoggers) {
  logging::init(null_sink());
  ASSERT_NE(spdlog::get("engine"), nullptr);

  logging::shutdown();
  EXPECT_EQ(spdlog::get("engine"), nullptr);
}
