// residency.hpp - residency/eviction policy configuration.
#pragma once

#include <cstdint>
#include <string>

namespace kernelcache {

// Configurable residency policy considering cost, reuse, pin and priority.
struct ResidencyPolicy {
  std::uint64_t host_budget_bytes = 1ull << 30;   // 1 GiB
  std::uint64_t device_budget_bytes = 1ull << 30; // 1 GiB
  bool apply_host_budget = true;
  bool apply_device_budget = true;
  bool compile_cost_aware = true;
  bool reload_cost_aware = true;
  bool size_aware = true;
  bool clock_based = true;      // use recency
  bool frequency_based = true;  // use access frequency
  double expected_reuse_weight = 1.0;
  double namespace_priority = 1.0;
  std::string policy_name = "default";
};

// Eviction score components, exposed for explain output. The cache never makes
// correctness depend on a single opaque scalar.
struct EvictionScore {
  double recency = 0.0;
  double frequency = 0.0;
  double compile_cost = 0.0;
  double reload_cost = 0.0;
  double size_cost = 0.0;
  double device_footprint = 0.0;
  double pin_penalty = 0.0;
  double expected_reuse = 0.0;
  double namespace_priority = 0.0;
  double total = 0.0;
};

struct EvictionCandidate {
  std::uint64_t artifact_lo = 0;
  std::uint64_t artifact_hi = 0;
  EvictionScore score;
  std::string tier;
  std::string reason;
};

}  // namespace kernelcache
