#pragma once

#include <concepts>

template <typename T>
concept MarketPublisherLike = requires(T t) {
  { t.start() } -> std::same_as<void>;
  { t.stop() } -> std::same_as<void>;
};
