#pragma once

#include "MarketEvent.hpp"
#include "Utils.hpp"
#include <concepts>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace detail {
struct EmitProbe {
  void operator()(const MarketEvent &, std::string_view) const;
};
struct AuthProbe {
  void operator()() const;
};
} // namespace detail

template <typename T>
concept MarketDataSourceLike =
    requires(T t, void (*cb)(void *, std::span<const char>), void *ctx) {
      { t.start() } -> std::same_as<void>;
      { t.stop() } -> std::same_as<void>;
      { t.setOnData(cb, ctx) } -> std::same_as<void>;
    };

template <typename T>
concept MarketDataDecoderLike =
    requires(T t, std::span<const char> data, uint64_t arrived_at,
             detail::EmitProbe emit) {
      { t.decode(data, arrived_at, emit) } -> std::same_as<void>;
    };

template <typename T>
concept MarketEventSinkLike =
    requires(T t, const MarketEvent &event, std::string_view symbol) {
      { t.start() } -> std::same_as<void>;
      { t.stop() } -> std::same_as<void>;
      { t.publish(event, symbol) } -> std::same_as<void>;
    };

template <MarketDataSourceLike SourceType, MarketDataDecoderLike DecoderType,
          MarketEventSinkLike SinkType>
class MarketDataPipeline {
public:
  MarketDataPipeline(SourceType source, DecoderType decoder, SinkType sink)
      : source_(std::move(source)), decoder_(std::move(decoder)),
        sink_(std::move(sink)) {
    if constexpr (requires {
                    source_.sendSubscribe();
                    decoder_.setOnAuthSuccess(detail::AuthProbe{});
                  }) {
      decoder_.setOnAuthSuccess([this]() { source_.sendSubscribe(); });
    }

    source_.setOnData(&onSourceData, this);
  }

  MarketDataPipeline(MarketDataPipeline &&) = delete;
  MarketDataPipeline &operator=(MarketDataPipeline &&) = delete;
  MarketDataPipeline(const MarketDataPipeline &) = delete;
  MarketDataPipeline &operator=(const MarketDataPipeline &) = delete;

  ~MarketDataPipeline() = default;

  void start() {
    sink_.start();
    source_.start();
  }

  void stop() {
    source_.stop();
    sink_.stop();
  }

private:
  static void onSourceData(void *ctx, std::span<const char> data) {
    auto *self = static_cast<MarketDataPipeline *>(ctx);
    const uint64_t arrived_at = nowNanos();
    self->decoder_.decode(
        data, arrived_at,
        [self](const MarketEvent &event, std::string_view symbol) {
          self->sink_.publish(event, symbol);
        });
  }

  SourceType source_;
  DecoderType decoder_;
  SinkType sink_;
};
