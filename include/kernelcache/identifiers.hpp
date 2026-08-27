// identifiers.hpp - strong typed identities and generations for KernelCache.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <compare>
#include <functional>
#include <ostream>

#include "kernelcache/result.hpp"

namespace kernelcache {

// ---------------------------------------------------------------------------
// StrongId128 - a lossless 128-bit identity carried as two 64-bit words.
// Every KernelCache identity is a distinct, non-interconvertible type derived
// from this base so that e.g. an ArtifactId can never be used as a KernelId.
// ---------------------------------------------------------------------------
class StrongId128 {
 public:
  StrongId128() = default;
  StrongId128(std::uint64_t hi, std::uint64_t lo) noexcept : hi_(hi), lo_(lo) {}

  // Parse canonical "hhhh....hhhh" (exactly 32 hex digits) or "a-b" decimal form.
  static Result<StrongId128> parse(std::string_view s) noexcept;

  std::uint64_t hi() const noexcept { return hi_; }
  std::uint64_t lo() const noexcept { return lo_; }

  // Canonical 32-hex-digit string. Deterministic and lossless.
  std::string str() const;

  // Parity with the wire: two uint64, big-endian byte order for the first word
  // so that canonical order matches str() byte order.
  void to_bytes(std::uint8_t out[16]) const noexcept;
  static StrongId128 from_bytes(const std::uint8_t in[16]) noexcept;

  bool operator==(const StrongId128&) const noexcept = default;
  auto operator<=>(const StrongId128&) const noexcept = default;

  explicit operator bool() const noexcept { return (hi_ | lo_) != 0; }

 protected:
  std::uint64_t hi_ = 0;
  std::uint64_t lo_ = 0;
};

std::ostream& operator<<(std::ostream& os, const StrongId128& id);

// ---------------------------------------------------------------------------
// Generation / authority scalar types. All monotonic and ordered.
// ---------------------------------------------------------------------------
struct CacheGeneration {
  std::uint64_t value = 0;
  bool operator==(const CacheGeneration&) const noexcept = default;
  auto operator<=>(const CacheGeneration&) const noexcept = default;
};

struct ArtifactGeneration {
  std::uint64_t value = 0;
  bool operator==(const ArtifactGeneration&) const noexcept = default;
  auto operator<=>(const ArtifactGeneration&) const noexcept = default;
};

struct LoadGeneration {
  std::uint64_t value = 0;
  bool operator==(const LoadGeneration&) const noexcept = default;
  auto operator<=>(const LoadGeneration&) const noexcept = default;
};

struct ResidencyGeneration {
  std::uint64_t value = 0;
  bool operator==(const ResidencyGeneration&) const noexcept = default;
  auto operator<=>(const ResidencyGeneration&) const noexcept = default;
};

struct CoordinatorEpoch {
  std::uint64_t value = 0;
  bool operator==(const CoordinatorEpoch&) const noexcept = default;
  auto operator<=>(const CoordinatorEpoch&) const noexcept = default;
};

struct WorkerId {
  std::uint64_t value = 0;
  bool operator==(const WorkerId&) const noexcept = default;
  auto operator<=>(const WorkerId&) const noexcept = default;
};

struct WorkerBootId {
  StrongId128 value;
  bool operator==(const WorkerBootId&) const noexcept = default;
};

// ---------------------------------------------------------------------------
// ID type generator macro
// ---------------------------------------------------------------------------
#define KC_DEFINE_ID_TYPE(Name)                                     \
  struct Name : StrongId128 {                                      \
    using StrongId128::StrongId128;                                \
    Name() = default;                                              \
    bool operator==(const Name& o) const noexcept = default;       \
    auto operator<=>(const Name& o) const noexcept = default;      \
  }

KC_DEFINE_ID_TYPE(KernelId);
KC_DEFINE_ID_TYPE(ArtifactId);
KC_DEFINE_ID_TYPE(BuildAttemptId);
KC_DEFINE_ID_TYPE(OperationId);
KC_DEFINE_ID_TYPE(ModelId);
KC_DEFINE_ID_TYPE(OperatorId);
KC_DEFINE_ID_TYPE(RequestId);
KC_DEFINE_ID_TYPE(NamespaceId);
KC_DEFINE_ID_TYPE(DeviceId);
KC_DEFINE_ID_TYPE(CompilerId);
KC_DEFINE_ID_TYPE(RuntimeId);

#undef KC_DEFINE_ID_TYPE

}  // namespace kernelcache


namespace std {
template <>
struct hash<kernelcache::StrongId128> {
  std::size_t operator()(const kernelcache::StrongId128& id) const noexcept {
    std::uint64_t h = id.hi() ^ (id.lo() * 0x9E3779B97F4A7C15ULL);
    return static_cast<std::size_t>(h);
  }
};
#define KC_HASH_ID(name) template<> struct hash<kernelcache::name> { std::size_t operator()(const kernelcache::name& id) const noexcept { return hash<kernelcache::StrongId128>{}(id); } };
KC_HASH_ID(KernelId); KC_HASH_ID(ArtifactId); KC_HASH_ID(BuildAttemptId); KC_HASH_ID(OperationId);
KC_HASH_ID(ModelId); KC_HASH_ID(OperatorId); KC_HASH_ID(RequestId); KC_HASH_ID(NamespaceId);
KC_HASH_ID(DeviceId); KC_HASH_ID(CompilerId); KC_HASH_ID(RuntimeId);
#undef KC_HASH_ID
}  // namespace std
