#pragma once

#include <cstdint>
#include <functional>
#include <msgpack.hpp>
#include <string>

namespace test_helpers {

inline std::string packQuoteMsgWithTimestamp(
    const char *sym, double bp, int64_t bs, double ap, int64_t as,
    std::function<void(msgpack::packer<msgpack::sbuffer> &)> pack_ts) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(1);
  pk.pack_map(7);
  pk.pack("T");
  pk.pack("q");
  pk.pack("S");
  pk.pack(std::string(sym));
  pk.pack("bp");
  pk.pack(bp);
  pk.pack("bs");
  pk.pack(bs);
  pk.pack("ap");
  pk.pack(ap);
  pk.pack("as");
  pk.pack(as);
  pk.pack("t");
  pack_ts(pk);
  return std::string(buf.data(), buf.size());
}

inline std::string packQuoteMsg(const char *sym, double bp, int64_t bs,
                                double ap, int64_t as) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(1);
  pk.pack_map(7);
  pk.pack("T");
  pk.pack("q");
  pk.pack("S");
  pk.pack(std::string(sym));
  pk.pack("bp");
  pk.pack(bp);
  pk.pack("bs");
  pk.pack(bs);
  pk.pack("ap");
  pk.pack(ap);
  pk.pack("as");
  pk.pack(as);
  pk.pack("t");
  pk.pack(static_cast<uint64_t>(1234567890000000000ULL));
  return std::string(buf.data(), buf.size());
}

inline std::string packTradeMsg(const char *sym, double price, int64_t size) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(1);
  pk.pack_map(5);
  pk.pack("T");
  pk.pack("t");
  pk.pack("S");
  pk.pack(std::string(sym));
  pk.pack("p");
  pk.pack(price);
  pk.pack("s");
  pk.pack(size);
  pk.pack("t");
  pk.pack(static_cast<uint64_t>(9876543210000000000ULL));
  return std::string(buf.data(), buf.size());
}

inline std::string packAuthResponse(bool success) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(&buf);
  pk.pack_array(1);
  pk.pack_map(1);
  pk.pack("T");
  pk.pack(success ? "success" : "error");
  return std::string(buf.data(), buf.size());
}

inline void writeBE32(uint8_t *dst, uint32_t val) {
  dst[0] = static_cast<uint8_t>(val >> 24);
  dst[1] = static_cast<uint8_t>(val >> 16);
  dst[2] = static_cast<uint8_t>(val >> 8);
  dst[3] = static_cast<uint8_t>(val);
}

inline void writeBE64(uint8_t *dst, uint64_t val) {
  dst[0] = static_cast<uint8_t>(val >> 56);
  dst[1] = static_cast<uint8_t>(val >> 48);
  dst[2] = static_cast<uint8_t>(val >> 40);
  dst[3] = static_cast<uint8_t>(val >> 32);
  dst[4] = static_cast<uint8_t>(val >> 24);
  dst[5] = static_cast<uint8_t>(val >> 16);
  dst[6] = static_cast<uint8_t>(val >> 8);
  dst[7] = static_cast<uint8_t>(val);
}

} // namespace test_helpers
