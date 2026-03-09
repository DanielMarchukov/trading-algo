#include "AlpacaMsgpackDecoder.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <msgpack.hpp>

namespace {

constexpr uint64_t kScalingFactor = 10000;
constexpr std::size_t kSymbolCapacity = 8;
constexpr double kMaxScaledPrice =
    static_cast<double>(std::numeric_limits<uint64_t>::max());
// 2^64 as double — any value >= this overflows uint64_t on cast.
const double kUint64UpperBound = std::ldexp(1.0, 64);

void copySymbol(char (&dest)[8], const char *src, uint32_t len) {
  std::memset(dest, 0, kSymbolCapacity);
  const auto n = (std::min)(static_cast<std::size_t>(len), kSymbolCapacity);
  if (n > 0) {
    std::memcpy(dest, src, n);
  }
}

[[nodiscard]] bool isSafePrice(double raw) {
  return std::isfinite(raw) && raw >= 0.0 &&
         raw * kScalingFactor < kMaxScaledPrice;
}

uint64_t decodeTimestampExt(const char *data, uint32_t ext_size) {
  const auto *d = reinterpret_cast<const uint8_t *>(data);
  uint32_t body_size = ext_size - 1;
  int8_t ext_type = static_cast<int8_t>(data[0]);
  const uint8_t *body = d + 1;

  if (ext_type != -1) {
    return 0;
  }

  if (body_size == 4) {
    uint32_t sec_be = 0;
    std::memcpy(&sec_be, body, 4);
#if defined(_WIN32)
    uint32_t sec = _byteswap_ulong(sec_be);
#else
    uint32_t sec = __builtin_bswap32(sec_be);
#endif
    return static_cast<uint64_t>(sec) * 1'000'000'000ULL;
  }
  if (body_size == 8) {
    uint64_t val_be = 0;
    std::memcpy(&val_be, body, 8);
#if defined(_WIN32)
    uint64_t val = _byteswap_uint64(val_be);
#else
    uint64_t val = __builtin_bswap64(val_be);
#endif
    uint32_t nsec30 = static_cast<uint32_t>(val >> 34);
    uint64_t sec34 = val & 0x3FFFFFFFFULL;
    return sec34 * 1'000'000'000ULL + nsec30;
  }
  if (body_size == 12) {
    uint32_t nsec_be = 0;
    std::memcpy(&nsec_be, body, 4);
    uint64_t sec_be64 = 0;
    std::memcpy(&sec_be64, body + 4, 8);
#if defined(_WIN32)
    uint32_t nsec = _byteswap_ulong(nsec_be);
    uint64_t sec = _byteswap_uint64(sec_be64);
#else
    uint32_t nsec = __builtin_bswap32(nsec_be);
    uint64_t sec = __builtin_bswap64(sec_be64);
#endif
    return sec * 1'000'000'000ULL + nsec;
  }
  return 0;
}

enum class FieldId : uint8_t {
  kNone,
  kType,
  kSymbol,
  kTimestamp,
  kBidPrice,
  kBidSize,
  kAskPrice,
  kAskSize
};

struct AlpacaVisitor : msgpack::null_visitor {
  using RawEmitFn = AlpacaMsgpackDecoder::RawEmitFn;
  using AuthSuccessCallback = std::function<void()>;

  MarketEvent *event_buffer;
  RawEmitFn emit_fn;
  void *emit_ctx;
  const AuthSuccessCallback *on_auth_success;
  uint64_t arrived_at;

  uint32_t array_depth = 0;
  uint32_t array_index = 0;
  uint32_t map_depth = 0;
  bool in_key = false;
  FieldId current_field = FieldId::kNone;

  char msg_type[8] = {};
  uint32_t msg_type_len = 0;
  char symbol_buf[8] = {};
  uint32_t symbol_len = 0;
  uint64_t timestamp = 0;
  double p1_raw = 0.0;
  uint64_t s1_raw = 0;
  double p2_raw = 0.0;
  uint64_t s2_raw = 0;
  bool has_timestamp = false;
  bool invalid_item = false;

  void resetItem() {
    msg_type_len = 0;
    symbol_len = 0;
    timestamp = 0;
    p1_raw = 0.0;
    s1_raw = 0;
    p2_raw = 0.0;
    s2_raw = 0;
    has_timestamp = false;
    invalid_item = false;
    current_field = FieldId::kNone;
  }

  void emitItem() {
    if (invalid_item) {
      return;
    }

    std::string_view mt(msg_type, msg_type_len);
    std::string_view sym(symbol_buf, symbol_len);

    if (mt == "success") {
      if (array_index == 0 && on_auth_success && *on_auth_success) {
        (*on_auth_success)();
      }
      return;
    }

    if (mt.empty() || sym.empty()) {
      return;
    }

    if (mt == "q") {
      if (!isSafePrice(p1_raw) || !isSafePrice(p2_raw)) {
        return;
      }
      event_buffer->eventType = 1;
      event_buffer->p1 = static_cast<uint64_t>(p1_raw * kScalingFactor);
      event_buffer->s1 = s1_raw;
      event_buffer->p2 = static_cast<uint64_t>(p2_raw * kScalingFactor);
      event_buffer->s2 = s2_raw;
    } else if (mt == "t") {
      if (!isSafePrice(p1_raw)) {
        return;
      }
      event_buffer->eventType = 2;
      event_buffer->p1 = static_cast<uint64_t>(p1_raw * kScalingFactor);
      event_buffer->s1 = s1_raw;
      event_buffer->p2 = 0;
      event_buffer->s2 = 0;
    } else {
      return;
    }

    copySymbol(event_buffer->symbol, symbol_buf, symbol_len);
    event_buffer->timestamp = has_timestamp ? timestamp : 0;
    event_buffer->arrivedAt = arrived_at;
    emit_fn(emit_ctx, *event_buffer, sym);
  }

  bool start_array(uint32_t) {
    ++array_depth;
    array_index = 0;
    return true;
  }

  bool start_array_item() { return true; }

  bool end_array_item() {
    if (array_depth == 1) {
      ++array_index;
    }
    return true;
  }

  bool end_array() {
    --array_depth;
    return true;
  }

  bool start_map(uint32_t) {
    ++map_depth;
    if (array_depth == 1 && map_depth == 1) {
      resetItem();
    }
    return true;
  }

  bool start_map_key() {
    in_key = true;
    current_field = FieldId::kNone;
    return true;
  }

  bool end_map_key() {
    in_key = false;
    return true;
  }

  bool start_map_value() { return true; }

  bool end_map_value() {
    current_field = FieldId::kNone;
    return true;
  }

  bool end_map() {
    if (array_depth == 1 && map_depth == 1) {
      emitItem();
    }
    --map_depth;
    return true;
  }

  bool visit_str(const char *v, uint32_t size) {
    if (map_depth != 1) {
      return true;
    }

    if (in_key) {
      if (size == 1) {
        switch (v[0]) {
        case 'T':
          current_field = FieldId::kType;
          break;
        case 'S':
          current_field = FieldId::kSymbol;
          break;
        case 't':
          current_field = FieldId::kTimestamp;
          break;
        case 'p':
          current_field = FieldId::kBidPrice;
          break;
        case 's':
          current_field = FieldId::kBidSize;
          break;
        default:
          current_field = FieldId::kNone;
          break;
        }
      } else if (size == 2) {
        if (v[0] == 'b' && v[1] == 'p') {
          current_field = FieldId::kBidPrice;
        } else if (v[0] == 'b' && v[1] == 's') {
          current_field = FieldId::kBidSize;
        } else if (v[0] == 'a' && v[1] == 'p') {
          current_field = FieldId::kAskPrice;
        } else if (v[0] == 'a' && v[1] == 's') {
          current_field = FieldId::kAskSize;
        } else {
          current_field = FieldId::kNone;
        }
      } else {
        current_field = FieldId::kNone;
      }
      return true;
    }

    switch (current_field) {
    case FieldId::kType:
      msg_type_len = (std::min)(size, static_cast<uint32_t>(sizeof(msg_type)));
      std::memcpy(msg_type, v, msg_type_len);
      break;
    case FieldId::kSymbol:
      symbol_len = (std::min)(size, static_cast<uint32_t>(sizeof(symbol_buf)));
      std::memcpy(symbol_buf, v, symbol_len);
      break;
    default:
      break;
    }
    return true;
  }

  bool visit_positive_integer(uint64_t v) {
    if (map_depth != 1 || in_key) {
      return true;
    }
    switch (current_field) {
    case FieldId::kTimestamp:
      timestamp = v;
      has_timestamp = true;
      break;
    case FieldId::kBidPrice:
      p1_raw = static_cast<double>(v);
      break;
    case FieldId::kBidSize:
      s1_raw = v;
      break;
    case FieldId::kAskPrice:
      p2_raw = static_cast<double>(v);
      break;
    case FieldId::kAskSize:
      s2_raw = v;
      break;
    default:
      break;
    }
    return true;
  }

  bool visit_negative_integer(int64_t) {
    if (map_depth != 1 || in_key) {
      return true;
    }
    switch (current_field) {
    case FieldId::kTimestamp:
      timestamp = 0;
      has_timestamp = true;
      break;
    case FieldId::kBidPrice:
    case FieldId::kBidSize:
    case FieldId::kAskPrice:
    case FieldId::kAskSize:
      invalid_item = true;
      break;
    default:
      break;
    }
    return true;
  }

  bool visit_float32(float v) { return visit_float64(static_cast<double>(v)); }

  bool visit_float64(double v) {
    if (map_depth != 1 || in_key) {
      return true;
    }
    switch (current_field) {
    case FieldId::kTimestamp:
      if (!std::isfinite(v) || v < 0.0 || v >= kUint64UpperBound) {
        timestamp = 0;
      } else {
        timestamp = static_cast<uint64_t>(v);
      }
      has_timestamp = true;
      break;
    case FieldId::kBidPrice:
      p1_raw = v;
      break;
    case FieldId::kBidSize:
      if (!std::isfinite(v) || v < 0.0 || v >= kUint64UpperBound) {
        invalid_item = true;
      } else {
        s1_raw = static_cast<uint64_t>(v);
      }
      break;
    case FieldId::kAskPrice:
      p2_raw = v;
      break;
    case FieldId::kAskSize:
      if (!std::isfinite(v) || v < 0.0 || v >= kUint64UpperBound) {
        invalid_item = true;
      } else {
        s2_raw = static_cast<uint64_t>(v);
      }
      break;
    default:
      break;
    }
    return true;
  }

  bool visit_ext(const char *v, uint32_t size) {
    if (map_depth != 1 || in_key) {
      return true;
    }
    if (current_field == FieldId::kTimestamp && size >= 2) {
      timestamp = decodeTimestampExt(v, size);
      has_timestamp = true;
    }
    return true;
  }

  void parse_error(size_t, size_t) {}
  void insufficient_bytes(size_t, size_t) {}
};

} // namespace

void AlpacaMsgpackDecoder::setOnAuthSuccess(AuthSuccessCallback cb) {
  on_auth_success_ = std::move(cb);
}

void AlpacaMsgpackDecoder::decodeRaw(std::span<const char> data,
                                     uint64_t arrived_at, RawEmitFn emit_fn,
                                     void *emit_ctx) {
  AlpacaVisitor visitor;
  visitor.event_buffer = &event_buffer_;
  visitor.emit_fn = emit_fn;
  visitor.emit_ctx = emit_ctx;
  visitor.on_auth_success = &on_auth_success_;
  visitor.arrived_at = arrived_at;

  try {
    msgpack::parse(data.data(), data.size(), visitor);
  } catch (...) {
    // msgpack::parse() can throw on severely malformed input despite
    // SAX visitor callbacks. Swallow here to avoid crashing the
    // data pipeline — the malformed frame is simply dropped.
  }
}
