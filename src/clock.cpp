#include "kernelcache/clock.hpp"

#include <chrono>

namespace kernelcache {

std::uint64_t SystemClock::now_ns() const {
  auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

std::uint64_t SystemClock::wall_ms() const {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

}  // namespace kernelcache
