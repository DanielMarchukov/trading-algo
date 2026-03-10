#include "MarketDataPipeline.hpp"
#include "MarketEvent.hpp"
#include <cstring>
#include <functional>
#include <gtest/gtest.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class TestSource {
public:
  using RawDataFn = void (*)(void *, std::span<const char>);

  void start() { started_ = true; }
  void stop() { stopped_ = true; }
  void setOnData(RawDataFn fn, void *ctx) {
    on_data_fn_ = fn;
    on_data_ctx_ = ctx;
  }
  void sendSubscribe() { subscribe_called_ = true; }

  void injectData(const std::string &data) {
    if (on_data_fn_) {
      on_data_fn_(on_data_ctx_,
                  std::span<const char>(data.data(), data.size()));
    }
  }

  bool started_ = false;
  bool stopped_ = false;
  bool subscribe_called_ = false;

private:
  RawDataFn on_data_fn_ = nullptr;
  void *on_data_ctx_ = nullptr;
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

using TestPipeline = MarketDataPipeline<TestSource, TestDecoder, TestSink>;

class MarketDataPipelineTest : public ::testing::Test {};

TEST_F(MarketDataPipelineTest, ConstructsAndStartsStops) {
  TestPipeline pipeline{TestSource{}, TestDecoder{}, TestSink{}};

  pipeline.start();
  pipeline.stop();
}

TEST_F(MarketDataPipelineTest, DataFlowsFromSourceThroughDecoderToSink) {
  struct DataFlowState {
    bool data_decoded = false;
    bool event_published = false;
    std::string published_symbol;
    uint64_t published_arrived_at = 0;
  };
  auto state = std::make_shared<DataFlowState>();

  struct ExternalCallback {
    void (*fn)(void *, std::span<const char>) = nullptr;
    void *ctx = nullptr;
  };
  auto inject = std::make_shared<ExternalCallback>();

  struct InjectableSource {
    using RawDataFn = void (*)(void *, std::span<const char>);
    std::shared_ptr<ExternalCallback> inject_;
    void start() {}
    void stop() {}
    void setOnData(RawDataFn fn, void *ctx) {
      if (inject_) {
        inject_->fn = fn;
        inject_->ctx = ctx;
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
    void publish(const MarketEvent &event, std::string_view symbol) {
      state_->event_published = true;
      state_->published_symbol = std::string(symbol);
      state_->published_arrived_at = event.arrivedAt;
    }
  };

  InjectableSource src;
  src.inject_ = inject;

  MarketDataPipeline<InjectableSource, PassthroughDecoder, CapturingSink>
      pipeline(std::move(src), PassthroughDecoder{state}, CapturingSink{state});

  pipeline.start();
  pipeline.start();

  std::string test_data = "test_payload";
  ASSERT_NE(inject->fn, nullptr);
  ASSERT_NE(inject->ctx, nullptr);
  inject->fn(inject->ctx,
             std::span<const char>(test_data.data(), test_data.size()));

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
    using RawDataFn = void (*)(void *, std::span<const char>);
    std::shared_ptr<SharedState> state_;
    void start() {}
    void stop() {}
    void setOnData(RawDataFn, void *) {}
    void sendSubscribe() { state_->subscribe_called = true; }
  };

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
    void decode(std::span<const char>, uint64_t, const EmitCallback &) {}
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

  ASSERT_TRUE(*trigger);
  (*trigger)();

  EXPECT_TRUE(state->subscribe_called);
}

TEST_F(MarketDataPipelineTest, StopBeforeStartIsSafe) {
  TestPipeline pipeline{TestSource{}, TestDecoder{}, TestSink{}};
  EXPECT_NO_THROW(pipeline.stop());
}
