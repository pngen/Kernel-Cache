#include "kernelcache/canonical.hpp"
#include <cstring>

namespace kernelcache {

namespace {
inline void push_be_u64(std::vector<std::uint8_t>& v, std::uint64_t x) {
  v.push_back(static_cast<std::uint8_t>((x >> 56) & 0xffu));
  v.push_back(static_cast<std::uint8_t>((x >> 48) & 0xffu));
  v.push_back(static_cast<std::uint8_t>((x >> 40) & 0xffu));
  v.push_back(static_cast<std::uint8_t>((x >> 32) & 0xffu));
  v.push_back(static_cast<std::uint8_t>((x >> 24) & 0xffu));
  v.push_back(static_cast<std::uint8_t>((x >> 16) & 0xffu));
  v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xffu));
  v.push_back(static_cast<std::uint8_t>(x & 0xffu));
}
}  // namespace

CanonicalWriter& CanonicalWriter::tag(std::uint8_t t) { buf_.push_back(t); return *this; }
CanonicalWriter& CanonicalWriter::u8(std::uint8_t v) { buf_.push_back(v); return *this; }
CanonicalWriter& CanonicalWriter::u16(std::uint16_t v) {
  buf_.push_back(static_cast<std::uint8_t>((v >> 8) & 0xffu));
  buf_.push_back(static_cast<std::uint8_t>(v & 0xffu));
  return *this;
}
CanonicalWriter& CanonicalWriter::u32(std::uint32_t v) {
  for (int i = 3; i >= 0; --i) buf_.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xffu));
  return *this;
}
CanonicalWriter& CanonicalWriter::u64(std::uint64_t v) { push_be_u64(buf_, v); return *this; }
CanonicalWriter& CanonicalWriter::i64(std::int64_t v) {
  return u64(static_cast<std::uint64_t>(v));
}
CanonicalWriter& CanonicalWriter::boolean(bool v) { buf_.push_back(v ? 1u : 0u); return *this; }
CanonicalWriter& CanonicalWriter::str(std::string_view s) {
  u64(static_cast<std::uint64_t>(s.size()));
  buf_.insert(buf_.end(), s.begin(), s.end());
  return *this;
}
CanonicalWriter& CanonicalWriter::bytes(std::span<const std::uint8_t> b) {
  u64(static_cast<std::uint64_t>(b.size()));
  buf_.insert(buf_.end(), b.begin(), b.end());
  return *this;
}

}  // namespace kernelcache
