#include "AlpacaWebSocketSource.hpp"
#include <atomic>
#include <cstring>
#include <gtest/gtest.h>
#include <span>
#include <string>
#include <vector>

class TestableSource : public AlpacaWebSocketSource {
public:
  using AlpacaWebSocketSource::AlpacaWebSocketSource;
  using AlpacaWebSocketSource::onMessage;
};

namespace {

ix::WebSocketMessagePtr makeMessage(ix::WebSocketMessageType type,
                                    const std::string &payload) {
  return std::make_unique<ix::WebSocketMessage>(
      type, payload, payload.size(), ix::WebSocketErrorInfo{},
      ix::WebSocketOpenInfo{}, ix::WebSocketCloseInfo{});
}

struct CaptureCallback {
  std::string captured;
  bool called = false;

  static void thunk(void *ctx, std::span<const char> data) {
    auto *self = static_cast<CaptureCallback *>(ctx);
    self->captured.assign(data.data(), data.size());
    self->called = true;
  }
};

} // namespace

class AlpacaWebSocketSourceTest : public ::testing::Test {
protected:
  void SetUp() override {
    source_ = std::make_unique<TestableSource>(
        "key", "secret", std::vector<std::string>{"AAPL"}, is_running_);
  }

  std::atomic<bool> is_running_{true};
  std::unique_ptr<TestableSource> source_;
};

TEST_F(AlpacaWebSocketSourceTest, MessageInvokesDataCallback) {
  CaptureCallback cb;
  source_->setOnData(&CaptureCallback::thunk, &cb);

  std::string payload = "test_market_data";
  auto msg = makeMessage(ix::WebSocketMessageType::Message, payload);
  source_->onMessage(msg);

  EXPECT_TRUE(cb.called);
  EXPECT_EQ(cb.captured, "test_market_data");
}

TEST_F(AlpacaWebSocketSourceTest, MessageWithNoCallbackDoesNotCrash) {
  std::string payload = "orphan_data";
  auto msg = makeMessage(ix::WebSocketMessageType::Message, payload);
  EXPECT_NO_THROW(source_->onMessage(msg));
}

TEST_F(AlpacaWebSocketSourceTest, MessageSkippedWhenNotRunning) {
  CaptureCallback cb;
  source_->setOnData(&CaptureCallback::thunk, &cb);
  is_running_.store(false);

  std::string payload = "should_be_ignored";
  auto msg = makeMessage(ix::WebSocketMessageType::Message, payload);
  source_->onMessage(msg);

  EXPECT_FALSE(cb.called);
}

TEST_F(AlpacaWebSocketSourceTest, ErrorMessageDoesNotInvokeCallback) {
  CaptureCallback cb;
  source_->setOnData(&CaptureCallback::thunk, &cb);

  std::string payload;
  auto msg = makeMessage(ix::WebSocketMessageType::Error, payload);
  source_->onMessage(msg);

  EXPECT_FALSE(cb.called);
}

TEST_F(AlpacaWebSocketSourceTest, CloseMessageDoesNotInvokeCallback) {
  CaptureCallback cb;
  source_->setOnData(&CaptureCallback::thunk, &cb);

  std::string payload;
  auto msg = makeMessage(ix::WebSocketMessageType::Close, payload);
  source_->onMessage(msg);

  EXPECT_FALSE(cb.called);
}
