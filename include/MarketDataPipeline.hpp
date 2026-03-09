#pragma once

#include "MarketEvent.hpp"
#include "Utils.hpp"
#include <atomic>
#include <concepts>
#include <cstdint>
#include <span>
#include <string_view>

template <typename T>
concept MarketDataSourceLike =
    requires(T t, void (*cb)(std::span<const char>)) {
      { t.start() } -> std::same_as<void>;
      { t.stop() } -> std::same_as<void>;
      { t.setOnData(cb) } -> std::same_as<void>;
    };

template <typename T>
concept MarketDataDecoderLike =
    requires(T t, std::span<const char> data, uint64_t arrived_at,
             void (*emit)(const MarketEvent &, std::string_view)) {
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
                    decoder_.setOnAuthSuccess(std::declval<void (*)()>());
                  }) {
      decoder_.setOnAuthSuccess([this]() { source_.sendSubscribe(); });
    }

    source_.setOnData([this](std::span<const char> data) {
      const uint64_t arrived_at = nowNanos();
      decoder_.decode(
          data, arrived_at,
          [this](const MarketEvent &event, std::string_view symbol) {
            sink_.publish(event, symbol);
          });
    });
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
  SourceType source_;
  DecoderType decoder_;
  SinkType sink_;
};
