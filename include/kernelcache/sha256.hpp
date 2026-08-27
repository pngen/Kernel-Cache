// sha256.hpp - deterministic SHA-256 used to digest canonical keys.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <span>
#include <array>

namespace kernelcache {

using Sha256Digest = std::array<std::uint8_t, 32>;

class Sha256 {
 public:
  Sha256();
  // Streaming interface.
  Sha256& update(std::span<const std::uint8_t> data);
  Sha256& update(std::string_view s);
  Sha256& update_byte(std::uint8_t byte);
  // Finalize and return the digest. The object may be reused after reset.
  Sha256Digest finish();

  static Sha256Digest digest(std::span<const std::uint8_t> data);
  static Sha256Digest digest(std::string_view s);

 private:
  void process_block(const std::uint8_t* block);
  void reset();
  std::uint32_t state_[8] = {};
  std::uint64_t bit_count_ = 0;
  std::uint8_t buffer_[64] = {};
  std::size_t buffer_used_ = 0;
};

// Hex rendering (lowercase, no separators).
std::string to_hex(const Sha256Digest& digest);

}  // namespace kernelcache
