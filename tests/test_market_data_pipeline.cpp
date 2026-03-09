#include "MarketDataPipeline.hpp"
#include "MarketEvent.hpp"
#include <cstring>
#include <functional>
#include <gtest/gtest.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// --- Test doubles ---

class TestSource {
public:
  void start() { started_ = true; }
  void stop() { stopped_ = true; }
  void setOnData(std::function<void(std::span<const char>)> cb) {
    on_data_ = std::move(cb);
  }
  void sendSubscribe() { subscribe_called_ = true; }

  // Test helper: inject raw data
  void injectData(const std::string &data) {
    if (on_data_) {
      on_data_(std::span<const char>(data.data(), data.size()));
    }
  }

  bool started_ = false;
  bool stopped_ = false;
  bool subscribe_called_ = false;

private:
  std::function<void(std::span<const char>)> on_data_;
};

class TestDecoder {
public:
  using EmitCallback =
      std::function<void(const MarketEvent &, std::string_view)>;
  using AuthSuccessCallback = std::function<void()>;

  void setOnAuthSuccess(AuthSuccessCallback cb) {
    on_auth_success_ = std::move(cb);
  }

  void decode(std::span<const char> data, uint64_t arrived_at,
              const EmitCallback &emit) {
    decode_count_++;
    last_arrived_at_ = arrived_at;
    last_data_ = std::string(data.data(), data.size());

    // Emit a test event for each decode call
    MarketEvent event{};
    event.eventType = 1;
    std::memcpy(event.symbol, "TEST", 4);
    event.arrivedAt = arrived_at;
    emit(event, "TEST");
  }

  void triggerAuthSuccess() {
    if (on_auth_success_) {
      on_auth_success_();
    }
  }

  uint32_t decode_count_ = 0;
  uint64_t last_arrived_at_ = 0;
  std::string last_data_;

private:
  AuthSuccessCallback on_auth_success_;
};

class TestSink {
public:
  void start() { started_ = true; }
  void stop() { stopped_ = true; }
  void publish(const MarketEvent &event, std::string_view symbol) {
    published_.emplace_back(event, std::string(symbol));
  }

  bool started_ = false;
  bool stopped_ = false;
  std::vector<std::pair<MarketEvent, std::string>> published_;
};

static_assert(MarketDataSourceLike<TestSource>);
static_assert(MarketDataDecoderLike<TestDecoder>);
static_assert(MarketEventSinkLike<TestSink>);

// --- Pipeline tests ---

using TestPipeline = MarketDataPipeline<TestSource, TestDecoder, TestSink>;

class MarketDataPipelineTest : public ::testing::Test {};

TEST_F(MarketDataPipelineTest, ConstructsAndStartsStops) {
  TestPipeline pipeline{TestSource{}, TestDecoder{}, TestSink{}};

  pipeline.start();
  pipeline.stop();
}

TEST_F(MarketDataPipelineTest, DataFlowsFromSourceThroughDecoderToSink) {
  // Shared state captures post-move pipeline activity
  struct DataFlowState {
    bool data_decoded = false;
    bool event_published = false;
    std::string published_symbol;
    uint64_t published_arrived_at = 0;
  };
  auto state = std::make_shared<DataFlowState>();

  // Source that exports its on_data callback via shared_ptr
  auto inject = std::make_shared<
      std::function<void(std::span<const char>)>>();
  TestSource source;
  // Replace setOnData to capture into shared inject pointer
  // — but TestSource already stores a callback. We need the
  //   pipeline constructor to wire it, so we use a thin wrapper.
  struct InjectableSource {
    std::shared_ptr<std::function<void(std::span<const char>)>>
        inject_;
    void start() {}
    void stop() {}
    void setOnData(
        std::function<void(std::span<const char>)> cb) {
      if (inject_) {
        *inject_ = std::move(cb);
      }
    }
    void sendSubscribe() {}
  };

  struct PassthroughDecoder {
    using EmitCallback =
        std::function<void(const MarketEvent &, std::string_view)>;
    using AuthSuccessCallback = std::function<void()>;
    std::shared_ptr<DataFlowState> state_;

    void setOnAuthSuccess(AuthSuccessCallback) {}
    void decode(std::span<const char>, uint64_t arrived_at,
                const EmitCallback &emit) {
      state_->data_decoded = true;
      MarketEvent event{};
      event.eventType = 1;
      event.arrivedAt = arrived_at;
      std::memcpy(event.symbol, "AAPL", 4);
      emit(event, "AAPL");
    }
  };

  struct CapturingSink {
    std::shared_ptr<DataFlowState> state_;
    void start() {}
    void stop() {}
    void publish(const MarketEvent &event,
                 std::string_view symbol) {
      state_->event_published = true;
      state_->published_symbol = std::string(symbol);
      state_->published_arrived_at = event.arrivedAt;
    }
  };

  InjectableSource src;
  src.inject_ = inject;

  MarketDataPipeline<InjectableSource, PassthroughDecoder,
                     CapturingSink>
      pipeline(std::move(src), PassthroughDecoder{state},
               CapturingSink{state});

  pipeline.start();

  std::string test_data = "test_payload";
  (*inject)(std::span<const char>(test_data.data(),
                                  test_data.size()));

  EXPECT_TRUE(state->data_decoded);
  EXPECT_TRUE(state->event_published);
  EXPECT_EQ(state->published_symbol, "AAPL");
  EXPECT_GT(state->published_arrived_at, 0ULL);

  pipeline.stop();
}

TEST_F(MarketDataPipelineTest, AuthSuccessCallbackWiresDecoderToSource) {
  struct SharedState {
    bool subscribe_called = false;
  };
  auto state = std::make_shared<SharedState>();

  struct SubscribeSource {
    std::shared_ptr<SharedState> state_;
    void start() {}
    void stop() {}
    void setOnData(std::function<void(std::span<const char>)>) {}
    void sendSubscribe() { state_->subscribe_called = true; }
  };

  // Decoder that exports the auth callback via shared_ptr so we can
  // invoke it after the decoder is moved into the pipeline
  struct AuthDecoder {
    using EmitCallback =
        std::function<void(const MarketEvent &, std::string_view)>;
    using AuthSuccessCallback = std::function<void()>;
    std::shared_ptr<std::function<void()>> trigger_;

    void setOnAuthSuccess(AuthSuccessCallback cb) {
      if (trigger_) {
        *trigger_ = std::move(cb);
      }
    }
    void decode(std::span<const char>, uint64_t,
                const EmitCallback &) {}
    void sendSubscribe() {}
  };

  struct NoopSink {
    void start() {}
    void stop() {}
    void publish(const MarketEvent &, std::string_view) {}
  };

  auto trigger = std::make_shared<std::function<void()>>();

  SubscribeSource source{state};
  AuthDecoder decoder;
  decoder.trigger_ = trigger;
  NoopSink sink;

  MarketDataPipeline<SubscribeSource, AuthDecoder, NoopSink> pipeline(
      std::move(source), std::move(decoder), std::move(sink));

  // Pipeline constructor wired setOnAuthSuccess -> sendSubscribe
  ASSERT_TRUE(*trigger);
  (*trigger)();

  EXPECT_TRUE(state->subscribe_called);
}

TEST_F(MarketDataPipelineTest, StopBeforeStartIsSafe) {
  TestPipeline pipeline{TestSource{}, TestDecoder{}, TestSink{}};
  EXPECT_NO_THROW(pipeline.stop());
}
