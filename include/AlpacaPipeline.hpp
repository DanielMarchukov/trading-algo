#pragma once

#include "AlpacaMsgpackDecoder.hpp"
#include "AlpacaWebSocketSource.hpp"
#include "MarketDataPipeline.hpp"
#include "MarketPublisher.hpp"
#include "ZmqMarketEventSink.hpp"

using AlpacaPipeline =
    MarketDataPipeline<AlpacaWebSocketSource, AlpacaMsgpackDecoder,
                       ZmqMarketEventSink>;

static_assert(MarketPublisherLike<AlpacaPipeline>,
              "AlpacaPipeline must satisfy MarketPublisherLike");
