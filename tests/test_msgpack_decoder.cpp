#include "AlpacaMsgpackDecoder.hpp"
#include "MarketEvent.hpp"
#include "MsgpackTestHelpers.hpp"
#include <cstring>
#include <gtest/gtest.h>
#include <span>
#include <string>
#include <vector>

using namespace test_helpers;

class MsgpackDecoderTest : public ::testing::Test {
protected:
  AlpacaMsgpackDecoder decoder_;
  std::vector<std::pair<MarketEvent, std::string>> captured_;

  void decode(const std::string &raw, uint64_t arrived_at = 999) {
    decoder_.decode(std::span<const char>(raw.data(), raw.size()), arrived_at,
                    [this](const MarketEvent &event, std::string_view symbol) {
                      captured_.emplace_back(event, std::string(symbol));
                    });
  }
};

// --- Parameterized event parsing tests ---

struct MarketEventParam {
  const char *description;
  std::function<std::string()> build_message;
  uint64_t expected_event_type;
  const char *expected_symbol;
  uint64_t expected_p1;
  uint64_t expected_s1;
  uint64_t expected_p2;
  uint64_t expected_s2;
};

class MarketEventParseTest
    : public MsgpackDecoderTest,
      public ::testing::WithParamInterface<MarketEventParam> {};

TEST_P(MarketEventParseTest, ParsesFieldsCorrectly) {
  const auto &param = GetParam();
  decode(param.build_message());

  ASSERT_EQ(captured_.size(), 1UL);
  const auto &[event, sym] = captured_[0];

  EXPECT_EQ(event.eventType, param.expected_event_type);
  EXPECT_EQ(std::string_view(event.symbol, strnlen(event.symbol, 8)),
            std::string_view(param.expected_symbol));
  EXPECT_EQ(event.p1, param.expected_p1);
  EXPECT_EQ(event.s1, param.expected_s1);
  EXPECT_EQ(event.p2, param.expected_p2);
  EXPECT_EQ(event.s2, param.expected_s2);
  EXPECT_EQ(event.arrivedAt, 999ULL);
}

INSTANTIATE_TEST_SUITE_P(
    EventTypes, MarketEventParseTest,
    ::testing::Values(
        MarketEventParam{
            "quote_all_fields",
            [] { return packQuoteMsg("AAPL", 150.25, 200, 150.30, 100); }, 1,
            "AAPL", 1502500, 200, 1503000, 100},
        MarketEventParam{"trade_all_fields",
                         [] { return packTradeMsg("GOOGL", 2800.50, 50); }, 2,
                         "GOOGL", 28005000, 50, 0, 0},
        MarketEventParam{"quote_zero_prices",
                         [] { return packQuoteMsg("AMZN", 0.0, 0, 0.0, 0); }, 1,
                         "AMZN", 0, 0, 0, 0},
        MarketEventParam{"trade_large_values",
                         [] { return packTradeMsg("TSLA", 99999.99, 10000); },
                         2, "TSLA", 999999900, 10000, 0, 0},
        MarketEventParam{"symbol_truncated_to_8_chars",
                         [] { return packTradeMsg("LONGERSYM", 100.0, 1); }, 2,
                         "LONGERSY", 1000000, 1, 0, 0}),
    [](const ::testing::TestParamInfo<MarketEventParam> &info) {
      return info.param.description;
    });

// --- Edge case tests ---

TEST_F(MsgpackDecoderTest, IgnoresEmptyArray) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(0);
  decode(std::string(buf.data(), buf.size()));

  EXPECT_TRUE(captured_.empty());
}

TEST_F(MsgpackDecoderTest, IgnoresNonArray) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_map(1);
  pk.pack("key");
  pk.pack("value");
  decode(std::string(buf.data(), buf.size()));

  EXPECT_TRUE(captured_.empty());
}

TEST_F(MsgpackDecoderTest, IgnoresMissingType) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(1);
  pk.pack_map(1);
  pk.pack("S");
  pk.pack("AAPL");
  decode(std::string(buf.data(), buf.size()));

  EXPECT_TRUE(captured_.empty());
}

TEST_F(MsgpackDecoderTest, IgnoresMissingSymbol) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(1);
  pk.pack_map(1);
  pk.pack("T");
  pk.pack("q");
  decode(std::string(buf.data(), buf.size()));

  EXPECT_TRUE(captured_.empty());
}

TEST_F(MsgpackDecoderTest, IgnoresUnknownType) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(1);
  pk.pack_map(2);
  pk.pack("T");
  pk.pack("b");
  pk.pack("S");
  pk.pack("AAPL");
  decode(std::string(buf.data(), buf.size()));

  EXPECT_TRUE(captured_.empty());
}

TEST_F(MsgpackDecoderTest, HandlesInvalidMsgpack) {
  decode("this is not valid msgpack \xff\xfe");

  EXPECT_TRUE(captured_.empty());
}

TEST_F(MsgpackDecoderTest, HandlesMultipleEventsInBatch) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(2);
  pk.pack_map(7);
  pk.pack("T");
  pk.pack("q");
  pk.pack("S");
  pk.pack("AAPL");
  pk.pack("bp");
  pk.pack(150.0);
  pk.pack("bs");
  pk.pack(static_cast<uint64_t>(100));
  pk.pack("ap");
  pk.pack(151.0);
  pk.pack("as");
  pk.pack(static_cast<uint64_t>(200));
  pk.pack("t");
  pk.pack(static_cast<uint64_t>(0));
  pk.pack_map(5);
  pk.pack("T");
  pk.pack("t");
  pk.pack("S");
  pk.pack("GOOGL");
  pk.pack("p");
  pk.pack(2800.0);
  pk.pack("s");
  pk.pack(static_cast<uint64_t>(10));
  pk.pack("t");
  pk.pack(static_cast<uint64_t>(0));

  decode(std::string(buf.data(), buf.size()));

  ASSERT_EQ(captured_.size(), 2UL);
  EXPECT_EQ(captured_[0].first.eventType, 1ULL);
  EXPECT_EQ(captured_[0].second, "AAPL");
  EXPECT_EQ(captured_[1].first.eventType, 2ULL);
  EXPECT_EQ(captured_[1].second, "GOOGL");
}

TEST_F(MsgpackDecoderTest, SkipsNonMapArrayElement) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(2);
  pk.pack("not a map");
  pk.pack_map(5);
  pk.pack("T");
  pk.pack("t");
  pk.pack("S");
  pk.pack("AAPL");
  pk.pack("p");
  pk.pack(100.0);
  pk.pack("s");
  pk.pack(static_cast<uint64_t>(10));
  pk.pack("t");
  pk.pack(static_cast<uint64_t>(0));

  decode(std::string(buf.data(), buf.size()));

  ASSERT_EQ(captured_.size(), 1UL);
  EXPECT_EQ(captured_[0].first.eventType, 2ULL);
  EXPECT_EQ(captured_[0].second, "AAPL");
}

TEST_F(MsgpackDecoderTest, SkipsNonStringMapKeys) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(1);
  pk.pack_map(4);
  pk.pack(42);
  pk.pack("ignored");
  pk.pack("T");
  pk.pack("t");
  pk.pack("S");
  pk.pack("AAPL");
  pk.pack("p");
  pk.pack(200.0);

  decode(std::string(buf.data(), buf.size()));

  ASSERT_EQ(captured_.size(), 1UL);
  EXPECT_EQ(captured_[0].first.eventType, 2ULL);
  EXPECT_EQ(captured_[0].first.p1, 2000000ULL);
}

TEST_F(MsgpackDecoderTest, PriceAsInteger) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(1);
  pk.pack_map(5);
  pk.pack("T");
  pk.pack("t");
  pk.pack("S");
  pk.pack("AAPL");
  pk.pack("p");
  pk.pack(static_cast<uint64_t>(150));
  pk.pack("s");
  pk.pack(static_cast<uint64_t>(10));
  pk.pack("t");
  pk.pack(static_cast<uint64_t>(0));

  decode(std::string(buf.data(), buf.size()));

  ASSERT_EQ(captured_.size(), 1UL);
  EXPECT_EQ(captured_[0].first.p1, 1500000ULL);
}

TEST_F(MsgpackDecoderTest, SizeAsFloat) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(1);
  pk.pack_map(5);
  pk.pack("T");
  pk.pack("t");
  pk.pack("S");
  pk.pack("AAPL");
  pk.pack("p");
  pk.pack(150.0);
  pk.pack("s");
  pk.pack(25.0);
  pk.pack("t");
  pk.pack(static_cast<uint64_t>(0));

  decode(std::string(buf.data(), buf.size()));

  ASSERT_EQ(captured_.size(), 1UL);
  EXPECT_EQ(captured_[0].first.s1, 25ULL);
}

TEST_F(MsgpackDecoderTest, QuoteIntegerPriceFloatSize) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(1);
  pk.pack_map(7);
  pk.pack("T");
  pk.pack("q");
  pk.pack("S");
  pk.pack("AAPL");
  pk.pack("bp");
  pk.pack(static_cast<uint64_t>(150));
  pk.pack("bs");
  pk.pack(100.0);
  pk.pack("ap");
  pk.pack(static_cast<uint64_t>(151));
  pk.pack("as");
  pk.pack(200.0);
  pk.pack("t");
  pk.pack(static_cast<uint64_t>(0));

  decode(std::string(buf.data(), buf.size()));

  ASSERT_EQ(captured_.size(), 1UL);
  EXPECT_EQ(captured_[0].first.eventType, 1ULL);
  EXPECT_EQ(captured_[0].first.p1, 1500000ULL);
  EXPECT_EQ(captured_[0].first.s1, 100ULL);
  EXPECT_EQ(captured_[0].first.p2, 1510000ULL);
  EXPECT_EQ(captured_[0].first.s2, 200ULL);
}

TEST_F(MsgpackDecoderTest, TimestampMissingDefaultsToZero) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(1);
  pk.pack_map(6);
  pk.pack("T");
  pk.pack("q");
  pk.pack("S");
  pk.pack("AAPL");
  pk.pack("bp");
  pk.pack(100.0);
  pk.pack("bs");
  pk.pack(static_cast<uint64_t>(1));
  pk.pack("ap");
  pk.pack(101.0);
  pk.pack("as");
  pk.pack(static_cast<uint64_t>(1));

  decode(std::string(buf.data(), buf.size()));

  ASSERT_EQ(captured_.size(), 1UL);
  EXPECT_EQ(captured_[0].first.timestamp, 0ULL);
}

// --- Auth success callback ---

TEST_F(MsgpackDecoderTest, AuthSuccessCallbackFires) {
  bool auth_called = false;
  decoder_.setOnAuthSuccess([&]() { auth_called = true; });
  decode(packAuthResponse(true));

  EXPECT_TRUE(auth_called);
  EXPECT_TRUE(captured_.empty());
}

TEST_F(MsgpackDecoderTest, AuthSuccessWithoutCallbackDoesNotCrash) {
  decode(packAuthResponse(true));
  EXPECT_TRUE(captured_.empty());
}

TEST_F(MsgpackDecoderTest, SuccessAtNonZeroIndexSkipsCallback) {
  bool auth_called = false;
  decoder_.setOnAuthSuccess([&]() { auth_called = true; });

  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(2);
  pk.pack_map(5);
  pk.pack("T");
  pk.pack("t");
  pk.pack("S");
  pk.pack("AAPL");
  pk.pack("p");
  pk.pack(100.0);
  pk.pack("s");
  pk.pack(static_cast<uint64_t>(1));
  pk.pack("t");
  pk.pack(static_cast<uint64_t>(0));
  pk.pack_map(1);
  pk.pack("T");
  pk.pack("success");

  decode(std::string(buf.data(), buf.size()));

  EXPECT_FALSE(auth_called);
  ASSERT_EQ(captured_.size(), 1UL);
  EXPECT_EQ(captured_[0].first.eventType, 2ULL);
}

TEST_F(MsgpackDecoderTest, AuthErrorDoesNotTriggerCallback) {
  bool auth_called = false;
  decoder_.setOnAuthSuccess([&]() { auth_called = true; });
  decode(packAuthResponse(false));
  EXPECT_FALSE(auth_called);
  EXPECT_TRUE(captured_.empty());
}

TEST_F(MsgpackDecoderTest, NegativePriceDropsEvent) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(1);
  pk.pack_map(5);
  pk.pack("T");
  pk.pack("t");
  pk.pack("S");
  pk.pack("AAPL");
  pk.pack("p");
  pk.pack(-1.0);
  pk.pack("s");
  pk.pack(static_cast<uint64_t>(10));
  pk.pack("t");
  pk.pack(static_cast<uint64_t>(0));

  decode(std::string(buf.data(), buf.size()));

  EXPECT_TRUE(captured_.empty());
}

struct TimestampParam {
  const char *description;
  std::function<std::string()> build_raw;
  uint64_t expected_nanos;
};

class TimestampDecodeTest
    : public MsgpackDecoderTest,
      public ::testing::WithParamInterface<TimestampParam> {};

TEST_P(TimestampDecodeTest, DecodesTimestampCorrectly) {
  const auto &param = GetParam();
  decode(param.build_raw());

  ASSERT_EQ(captured_.size(), 1UL);
  EXPECT_EQ(captured_[0].first.timestamp, param.expected_nanos);
}

INSTANTIATE_TEST_SUITE_P(
    Timestamps, TimestampDecodeTest,
    ::testing::Values(
        TimestampParam{"positive_integer",
                       [] {
                         return packQuoteMsgWithTimestamp(
                             "AAPL", 100.0, 1, 101.0, 1,
                             [](msgpack::packer<msgpack::sbuffer> &pk) {
                               pk.pack(static_cast<uint64_t>(5000000000ULL));
                             });
                       },
                       5000000000ULL},
        TimestampParam{"negative_integer_returns_zero",
                       [] {
                         return packQuoteMsgWithTimestamp(
                             "AAPL", 100.0, 1, 101.0, 1,
                             [](msgpack::packer<msgpack::sbuffer> &pk) {
                               pk.pack(static_cast<int64_t>(-1));
                             });
                       },
                       0ULL},
        TimestampParam{"float_timestamp",
                       [] {
                         return packQuoteMsgWithTimestamp(
                             "AAPL", 100.0, 1, 101.0, 1,
                             [](msgpack::packer<msgpack::sbuffer> &pk) {
                               pk.pack(1234567890.0);
                             });
                       },
                       1234567890ULL},
        TimestampParam{"ext4_seconds_only",
                       [] {
                         return packQuoteMsgWithTimestamp(
                             "AAPL", 100.0, 1, 101.0, 1,
                             [](msgpack::packer<msgpack::sbuffer> &pk) {
                               uint8_t data[4];
                               writeBE32(data, 1000);
                               pk.pack_ext(4, -1);
                               pk.pack_ext_body(
                                   reinterpret_cast<const char *>(data), 4);
                             });
                       },
                       1000ULL * 1'000'000'000ULL},
        TimestampParam{"ext8_combined",
                       [] {
                         return packQuoteMsgWithTimestamp(
                             "AAPL", 100.0, 1, 101.0, 1,
                             [](msgpack::packer<msgpack::sbuffer> &pk) {
                               uint64_t sec = 100;
                               uint32_t nsec = 500;
                               uint64_t val =
                                   (static_cast<uint64_t>(nsec) << 34) | sec;
                               uint8_t data[8];
                               writeBE64(data, val);
                               pk.pack_ext(8, -1);
                               pk.pack_ext_body(
                                   reinterpret_cast<const char *>(data), 8);
                             });
                       },
                       100ULL * 1'000'000'000ULL + 500ULL},
        TimestampParam{"ext12_separate_fields",
                       [] {
                         return packQuoteMsgWithTimestamp(
                             "AAPL", 100.0, 1, 101.0, 1,
                             [](msgpack::packer<msgpack::sbuffer> &pk) {
                               uint8_t data[12];
                               writeBE32(data, 999999999);
                               writeBE64(data + 4, 42);
                               pk.pack_ext(12, -1);
                               pk.pack_ext_body(
                                   reinterpret_cast<const char *>(data), 12);
                             });
                       },
                       42ULL * 1'000'000'000ULL + 999999999ULL},
        TimestampParam{"ext_wrong_type_returns_zero",
                       [] {
                         return packQuoteMsgWithTimestamp(
                             "AAPL", 100.0, 1, 101.0, 1,
                             [](msgpack::packer<msgpack::sbuffer> &pk) {
                               uint8_t data[4] = {0, 0, 0, 42};
                               pk.pack_ext(4, 5);
                               pk.pack_ext_body(
                                   reinterpret_cast<const char *>(data), 4);
                             });
                       },
                       0ULL},
        TimestampParam{"ext_unknown_size_returns_zero",
                       [] {
                         return packQuoteMsgWithTimestamp(
                             "AAPL", 100.0, 1, 101.0, 1,
                             [](msgpack::packer<msgpack::sbuffer> &pk) {
                               uint8_t data[6] = {0, 0, 0, 0, 0, 42};
                               pk.pack_ext(6, -1);
                               pk.pack_ext_body(
                                   reinterpret_cast<const char *>(data), 6);
                             });
                       },
                       0ULL},
        TimestampParam{"string_timestamp_returns_zero",
                       [] {
                         return packQuoteMsgWithTimestamp(
                             "AAPL", 100.0, 1, 101.0, 1,
                             [](msgpack::packer<msgpack::sbuffer> &pk) {
                               pk.pack("2024-01-15T10:30:00Z");
                             });
                       },
                       0ULL},
        TimestampParam{"nil_timestamp_returns_zero",
                       [] {
                         return packQuoteMsgWithTimestamp(
                             "AAPL", 100.0, 1, 101.0, 1,
                             [](msgpack::packer<msgpack::sbuffer> &pk) {
                               pk.pack_nil();
                             });
                       },
                       0ULL}),
    [](const ::testing::TestParamInfo<TimestampParam> &info) {
      return info.param.description;
    });
