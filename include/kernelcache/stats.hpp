// stats.hpp - observability: stats, snapshot, events.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <mutex>

#include "kernelcache/descriptors.hpp"

namespace kernelcache {

// Aggregate counters. Each number is labeled by the doc/source: measured,
// derived, configured, or estimated. We store a tagged counter.
struct Stats {
  std::uint64_t lookups = 0;
  std::uint64_t exact_hits = 0;
  std::uint64_t compatible_hits = 0;
  std::uint64_t persistent_hits = 0;
  std::uint64_t host_hits = 0;
  std::uint64_t device_hits = 0;
  std::uint64_t misses = 0;
  std::uint64_t builds = 0;
  std::uint64_t build_dedup = 0;      // waiters on existing builds
  std::uint64_t validation_failures = 0;
  std::uint64_t invalidations = 0;
  std::uint64_t evictions = 0;
  std::uint64_t reloads = 0;
  std::uint64_t compile_ns = 0;       // measured
  std::uint64_t validation_ns = 0;    // measured
  std::uint64_t load_ns = 0;          // measured
  std::uint64_t lookup_ns = 0;        // measured
  std::uint64_t bytes_persisted = 0;
  std::uint64_t bytes_host_resident = 0;
  std::uint64_t bytes_device_resident = 0;
  bool bytes_device_estimated = false;
  std::uint64_t avoided_compile_count = 0;  // derived
  std::uint64_t avoided_compile_ns = 0;     // derived from measured cold vs warm
  std::uint64_t reuse_count = 0;
  std::uint64_t corruption_count = 0;
  std::uint64_t failed_builds = 0;
  std::uint64_t failed_loads = 0;
  std::uint64_t stale_generation_rejections = 0;
  std::uint64_t persistent_reloads = 0;
  std::uint64_t cuda_exec_validations = 0;
  std::uint64_t active_builds = 0;          // derived (current)
  std::uint64_t active_validations = 0;     // derived
  std::uint64_t active_leases = 0;          // derived
  std::uint64_t active_device_loads = 0;    // derived
  // ratio helpers
  double hit_ratio() const;                  // derived
};

// A single event in the bounded history.
struct Event {
  std::uint64_t seq = 0;
  std::uint64_t time_ns = 0;
  std::string type;
  std::string message;
};

class EventHistory {
 public:
  explicit EventHistory(std::size_t max = 4096);
  void record(Event ev);
  std::vector<Event> recent(std::size_t limit) const;
  std::size_t size() const;
 private:
  mutable std::mutex mtx_;
  std::deque<Event> events_;
  std::size_t max_;
  std::uint64_t seq_ = 0;
};

// Snapshot of artifact counts by state and generated stats.
struct Snapshot {
  std::uint64_t artifact_count = 0;
  std::uint64_t artifact_count_by_state[20] = {};   // indexed by ArtifactState
  std::uint64_t metadata_only_bytes = 0;
  std::uint64_t persistent_bytes = 0;
  std::uint64_t host_resident_bytes = 0;
  std::uint64_t device_resident_bytes = 0;
  bool device_bytes_estimated = false;
  Stats stats;
  std::uint64_t cache_generation = 0;
};

}  // namespace kernelcache
