#include "kernelcache/identifiers.hpp"
#include <sstream>
#include <iomanip>
#include <array>

namespace kernelcache {

Result<StrongId128> StrongId128::parse(std::string_view s) noexcept {
  if (s.size() != 32) return Result<StrongId128>(ErrorCode::MalformedFrame, "id must be 32 hex digits");
  std::uint64_t hi = 0, lo = 0;
  for (std::size_t i = 0; i < 32; ++i) {
    char c = s[i];
    std::uint64_t v;
    if (c >= '0' && c <= '9') v = static_cast<std::uint64_t>(c - '0');
    else if (c >= 'a' && c <= 'f') v = static_cast<std::uint64_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') v = static_cast<std::uint64_t>(c - 'A' + 10);
    else return Result<StrongId128>(ErrorCode::MalformedFrame, "invalid hex digit in id");
    if (i < 16) hi = (hi << 4) | v;
    else lo = (lo << 4) | v;
  }
  return StrongId128(hi, lo);
}

std::string StrongId128::str() const {
  static const char* h = "0123456789abcdef";
  std::string s; s.reserve(32);
  for (int i = 60; i >= 0; i -= 4) s.push_back(h[(hi_ >> i) & 0xf]);
  for (int i = 60; i >= 0; i -= 4) s.push_back(h[(lo_ >> i) & 0xf]);
  return s;
}

void StrongId128::to_bytes(std::uint8_t out[16]) const noexcept {
  for (int i = 0; i < 8; ++i) { out[i] = static_cast<std::uint8_t>((hi_ >> ((7 - i) * 8)) & 0xff); out[8 + i] = static_cast<std::uint8_t>((lo_ >> ((7 - i) * 8)) & 0xff); }
}
StrongId128 StrongId128::from_bytes(const std::uint8_t in[16]) noexcept {
  StrongId128 r;
  for (int i = 0; i < 8; ++i) { r.hi_ = (r.hi_ << 8) | in[i]; r.lo_ = (r.lo_ << 8) | in[8 + i]; }
  return r;
}

std::ostream& operator<<(std::ostream& os, const StrongId128& id) { return os << id.str(); }

}  // namespace kernelcache
