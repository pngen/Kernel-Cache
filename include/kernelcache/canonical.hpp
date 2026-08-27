// canonical.hpp - deterministic typed canonical encoder for cache keys.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <span>

namespace kernelcache {

// A deterministic, big-endian, length-prefixed binary encoder. Field order is
// caller-controlled so that semantically equivalent inputs always produce
// byte-identical output. Integers are written as fixed-width big-endian so that
// values are lossless and stable across platforms. Strings and byte-vectors are
// prefixed with a big-endian 64-bit length so that any lossless 64-bit length
// value is representable and unambiguous.
class CanonicalWriter {
 public:
  CanonicalWriter& tag(std::uint8_t t);
  CanonicalWriter& u8(std::uint8_t v);
  CanonicalWriter& u16(std::uint16_t v);
  CanonicalWriter& u32(std::uint32_t v);
  CanonicalWriter& u64(std::uint64_t v);
  CanonicalWriter& i64(std::int64_t v);
  CanonicalWriter& boolean(bool v);
  CanonicalWriter& str(std::string_view s);
  CanonicalWriter& bytes(std::span<const std::uint8_t> b);

  const std::vector<std::uint8_t>& buffer() const noexcept { return buf_; }
  std::size_t size() const noexcept { return buf_.size(); }
  std::string as_string() const { return std::string(buf_.begin(), buf_.end()); }

  void clear() { buf_.clear(); }

 private:
  std::vector<std::uint8_t> buf_;
};

}  // namespace kernelcache
