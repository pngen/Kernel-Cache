#include "kernelcache/stats.hpp"

namespace kernelcache {

double Stats::hit_ratio() const {
  std::uint64_t denominator = exact_hits + compatible_hits + persistent_hits +
                              host_hits + device_hits + misses;
  if (denominator == 0) return 0.0;
  return static_cast<double>(exact_hits + compatible_hits + persistent_hits +
                             host_hits + device_hits) / static_cast<double>(denominator);
}

EventHistory::EventHistory(std::size_t max) : max_(max) {}

void EventHistory::record(Event ev) {
  std::lock_guard<std::mutex> lk(mtx_);
  ev.seq = ++seq_;
  events_.push_back(std::move(ev));
  while (events_.size() > max_) events_.pop_front();
}

std::vector<Event> EventHistory::recent(std::size_t limit) const {
  std::lock_guard<std::mutex> lk(mtx_);
  std::vector<Event> out;
  std::size_t n = std::min(limit, events_.size());
  auto it = events_.end();
  std::advance(it, -static_cast<std::ptrdiff_t>(n));
  out.assign(it, events_.end());
  return out;
}

std::size_t EventHistory::size() const {
  std::lock_guard<std::mutex> lk(mtx_);
  return events_.size();
}

}  // namespace kernelcache
