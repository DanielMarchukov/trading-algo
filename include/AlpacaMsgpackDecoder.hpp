#pragma once

#include "MarketDataPipeline.hpp"
#include "MarketEvent.hpp"
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

class AlpacaMsgpackDecoder {
public:
  using AuthSuccessCallback = std::function<void()>;
  using RawEmitFn = void (*)(void *, const MarketEvent &, std::string_view);

  AlpacaMsgpackDecoder() = default;

  void setOnAuthSuccess(AuthSuccessCallback cb);

  template <typename EmitFn>
  void decode(std::span<const char> data, uint64_t arrived_at,
              const EmitFn &emit) {
    auto thunk = [](void *ctx, const MarketEvent &e, std::string_view s) {
      (*static_cast<const EmitFn *>(ctx))(e, s);
    };
    decodeRaw(data, arrived_at, thunk,
              const_cast<void *>(static_cast<const void *>(&emit)));
  }

private:
  void decodeRaw(std::span<const char> data, uint64_t arrived_at,
                 RawEmitFn emit_fn, void *emit_ctx);

  alignas(64) MarketEvent event_buffer_{};
  AuthSuccessCallback on_auth_success_;
};

static_assert(MarketDataDecoderLike<AlpacaMsgpackDecoder>,
              "AlpacaMsgpackDecoder must satisfy MarketDataDecoderLike");
