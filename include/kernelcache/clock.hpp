// clock.hpp - monotonic and wall-clock time source.
#pragma once

#include <cstdint>

namespace kernelcache {

class Clock {
 public:
  virtual ~Clock() = default;
  // Monotonic nanoseconds (never goes backwards). Zero is not a special value.
  virtual std::uint64_t now_ns() const = 0;
  // Milliseconds since epoch for wall-clock persistence.
  virtual std::uint64_t wall_ms() const = 0;
};

// Production clock backed by the OS.
class SystemClock final : public Clock {
 public:
  std::uint64_t now_ns() const override;
  std::uint64_t wall_ms() const override;
};

}  // namespace kernelcache
