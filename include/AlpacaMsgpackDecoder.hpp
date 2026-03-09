#pragma once

#include "MarketDataPipeline.hpp"
#include "MarketEvent.hpp"
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

class AlpacaMsgpackDecoder {
public:
  using EmitCallback =
      std::function<void(const MarketEvent &, std::string_view)>;
  using AuthSuccessCallback = std::function<void()>;

  AlpacaMsgpackDecoder() = default;

  void setOnAuthSuccess(AuthSuccessCallback cb);
  void decode(std::span<const char> data, uint64_t arrived_at,
              const EmitCallback &emit);

private:
  MarketEvent event_buffer_{};
  AuthSuccessCallback on_auth_success_;
};

static_assert(MarketDataDecoderLike<AlpacaMsgpackDecoder>,
              "AlpacaMsgpackDecoder must satisfy MarketDataDecoderLike");
