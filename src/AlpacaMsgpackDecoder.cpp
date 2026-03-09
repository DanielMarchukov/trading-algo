#include "AlpacaMsgpackDecoder.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <msgpack.hpp>

namespace {

constexpr uint64_t kScalingFactor = 10000;
constexpr std::size_t kSymbolCapacity = 8;

[[nodiscard]] uint64_t
decodeTimestampNanos(const msgpack::object &obj) noexcept {
  if (obj.type == msgpack::type::POSITIVE_INTEGER) {
    return obj.via.u64;
  }
  if (obj.type == msgpack::type::NEGATIVE_INTEGER) {
    return static_cast<uint64_t>(obj.via.i64);
  }
  if (obj.type == msgpack::type::FLOAT32 ||
      obj.type == msgpack::type::FLOAT64) {
    return static_cast<uint64_t>(obj.via.f64);
  }
  if (obj.type == msgpack::type::EXT) {
    const auto &ext = obj.via.ext;
    if (ext.type() != -1) {
      return 0;
    }
    const auto *data = reinterpret_cast<const uint8_t *>(ext.data());
    uint32_t size = ext.size;

    if (size == 4) {
      uint32_t sec_be = 0;
      std::memcpy(&sec_be, data, 4);
#if defined(_WIN32)
      uint32_t sec = _byteswap_ulong(sec_be);
#else
      uint32_t sec = __builtin_bswap32(sec_be);
#endif
      return static_cast<uint64_t>(sec) * 1'000'000'000ULL;
    }
    if (size == 8) {
      uint64_t val_be = 0;
      std::memcpy(&val_be, data, 8);
#if defined(_WIN32)
      uint64_t val = _byteswap_uint64(val_be);
#else
      uint64_t val = __builtin_bswap64(val_be);
#endif
      uint32_t nsec30 = static_cast<uint32_t>(val >> 34);
      uint64_t sec34 = val & 0x3FFFFFFFFULL;
      return sec34 * 1'000'000'000ULL + nsec30;
    }
    if (size == 12) {
      uint32_t nsec_be = 0;
      std::memcpy(&nsec_be, data, 4);
      uint64_t sec_be64 = 0;
      std::memcpy(&sec_be64, data + 4, 8);
#if defined(_WIN32)
      uint32_t nsec = _byteswap_ulong(nsec_be);
      uint64_t sec = _byteswap_uint64(sec_be64);
#else
      uint32_t nsec = __builtin_bswap32(nsec_be);
      uint64_t sec = __builtin_bswap64(sec_be64);
#endif
      return sec * 1'000'000'000ULL + nsec;
    }
  }
  return 0;
}

void copySymbol(char (&dest)[8], std::string_view src) {
  std::memset(dest, 0, kSymbolCapacity);
  const auto len = (std::min)(src.size(), kSymbolCapacity);
  if (len > 0) {
    std::memcpy(dest, src.data(), len);
  }
}

} // namespace

void AlpacaMsgpackDecoder::setOnAuthSuccess(AuthSuccessCallback cb) {
  on_auth_success_ = std::move(cb);
}

void AlpacaMsgpackDecoder::decode(std::span<const char> data,
                                   uint64_t arrived_at,
                                   const EmitCallback &emit) {
  try {
    msgpack::object_handle oh = msgpack::unpack(data.data(), data.size());
    const msgpack::object &root = oh.get();

    if (root.type != msgpack::type::ARRAY) {
      return;
    }

    for (uint32_t i = 0; i < root.via.array.size; ++i) {
      const msgpack::object &item = root.via.array.ptr[i];
      if (item.type != msgpack::type::MAP) {
        continue;
      }

      std::string_view msg_type;
      std::string_view symbol;
      const msgpack::object *ts_obj = nullptr;
      double p1_raw = 0.0;
      uint64_t s1_raw = 0;
      double p2_raw = 0.0;
      uint64_t s2_raw = 0;

      for (uint32_t k = 0; k < item.via.map.size; ++k) {
        const auto &kv = item.via.map.ptr[k];
        if (kv.key.type != msgpack::type::STR) {
          continue;
        }

        std::string_view key(kv.key.via.str.ptr, kv.key.via.str.size);

        if (key == "T" && kv.val.type == msgpack::type::STR) {
          msg_type =
              std::string_view(kv.val.via.str.ptr, kv.val.via.str.size);
        } else if (key == "S" && kv.val.type == msgpack::type::STR) {
          symbol =
              std::string_view(kv.val.via.str.ptr, kv.val.via.str.size);
        } else if (key == "t") {
          ts_obj = &kv.val;
        } else if (key == "bp" || key == "p") {
          if (kv.val.type == msgpack::type::FLOAT32 ||
              kv.val.type == msgpack::type::FLOAT64) {
            p1_raw = kv.val.via.f64;
          } else if (kv.val.type == msgpack::type::POSITIVE_INTEGER) {
            p1_raw = static_cast<double>(kv.val.via.u64);
          }
        } else if (key == "bs" || key == "s") {
          if (kv.val.type == msgpack::type::POSITIVE_INTEGER) {
            s1_raw = kv.val.via.u64;
          } else if (kv.val.type == msgpack::type::FLOAT32 ||
                     kv.val.type == msgpack::type::FLOAT64) {
            s1_raw = static_cast<uint64_t>(kv.val.via.f64);
          }
        } else if (key == "ap") {
          if (kv.val.type == msgpack::type::FLOAT32 ||
              kv.val.type == msgpack::type::FLOAT64) {
            p2_raw = kv.val.via.f64;
          } else if (kv.val.type == msgpack::type::POSITIVE_INTEGER) {
            p2_raw = static_cast<double>(kv.val.via.u64);
          }
        } else if (key == "as") {
          if (kv.val.type == msgpack::type::POSITIVE_INTEGER) {
            s2_raw = kv.val.via.u64;
          } else if (kv.val.type == msgpack::type::FLOAT32 ||
                     kv.val.type == msgpack::type::FLOAT64) {
            s2_raw = static_cast<uint64_t>(kv.val.via.f64);
          }
        }
      }

      // Auth/subscribe success has no symbol — handle before symbol check
      if (msg_type == "success") {
        if (i == 0 && on_auth_success_) {
          on_auth_success_();
        }
        continue;
      }

      if (msg_type.empty() || symbol.empty()) {
        continue;
      }

      if (msg_type == "q") {
        event_buffer_.eventType = 1;
        event_buffer_.p1 = static_cast<uint64_t>(p1_raw * kScalingFactor);
        event_buffer_.s1 = s1_raw;
        event_buffer_.p2 = static_cast<uint64_t>(p2_raw * kScalingFactor);
        event_buffer_.s2 = s2_raw;
      } else if (msg_type == "t") {
        event_buffer_.eventType = 2;
        event_buffer_.p1 = static_cast<uint64_t>(p1_raw * kScalingFactor);
        event_buffer_.s1 = s1_raw;
        event_buffer_.p2 = 0;
        event_buffer_.s2 = 0;
      } else {
        continue;
      }

      copySymbol(event_buffer_.symbol, symbol);
      event_buffer_.timestamp =
          ts_obj != nullptr ? decodeTimestampNanos(*ts_obj) : 0;
      event_buffer_.arrivedAt = arrived_at;

      emit(event_buffer_, symbol);
    }
  } catch (const std::exception &e) {
    std::cerr << "AlpacaMsgpackDecoder: decode error: " << e.what()
              << std::endl;
  } catch (...) {
    std::cerr << "AlpacaMsgpackDecoder: decode unknown error" << std::endl;
  }
}
