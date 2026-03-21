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

TEST_F(LoggingTest, InitRegistersAllNamedLoggers) {
  logging::init();

  EXPECT_NE(spdlog::get("engine"), nullptr);
  EXPECT_NE(spdlog::get("gateway"), nullptr);
  EXPECT_NE(spdlog::get("fills"), nullptr);
  EXPECT_NE(spdlog::get("rest"), nullptr);
  EXPECT_NE(spdlog::get("market"), nullptr);
  EXPECT_NE(spdlog::get("zmq"), nullptr);
  EXPECT_NE(spdlog::get("system"), nullptr);
  EXPECT_NE(spdlog::get("latency"), nullptr);

  logging::shutdown();
}

TEST_F(LoggingTest, InitSetsDefaultLoggerToSystem) {
  logging::init();

  EXPECT_EQ(spdlog::default_logger()->name(), "system");

  logging::shutdown();
}

TEST_F(LoggingTest, InitForTestsRegistersAllNamedLoggers) {
  logging::initForTests();

  EXPECT_NE(spdlog::get("engine"), nullptr);
  EXPECT_NE(spdlog::get("gateway"), nullptr);
  EXPECT_NE(spdlog::get("fills"), nullptr);
  EXPECT_NE(spdlog::get("rest"), nullptr);
  EXPECT_NE(spdlog::get("market"), nullptr);
  EXPECT_NE(spdlog::get("zmq"), nullptr);
  EXPECT_NE(spdlog::get("system"), nullptr);
  EXPECT_NE(spdlog::get("latency"), nullptr);
}

TEST_F(LoggingTest, InitForTestsSetsDefaultLoggerToSystem) {
  logging::initForTests();

  EXPECT_EQ(spdlog::default_logger()->name(), "system");
}

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
