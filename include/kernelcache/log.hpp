// log.hpp - minimal structured logging.
#pragma once

#include <string>
#include <sstream>
#include <mutex>
#include <iostream>

namespace kernelcache {
namespace log {

enum class Level { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4 };

void set_level(Level l) noexcept;
Level level() noexcept;

void write(Level l, const std::string& msg);

template <typename... Args>
void log(Level l, Args&&... args) {
  std::ostringstream os;
  (os << ... << std::forward<Args>(args));
  write(l, os.str());
}

}  // namespace log
}  // namespace kernelcache
