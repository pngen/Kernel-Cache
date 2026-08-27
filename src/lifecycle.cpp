#include "kernelcache/lifecycle.hpp"
#include <array>
#include <utility>

namespace kernelcache {

namespace {
constexpr std::pair<ArtifactState, ArtifactState> kTransitions[] = {
  {ArtifactState::Discovered, ArtifactState::Building},
  {ArtifactState::Discovered, ArtifactState::Invalidated},
  {ArtifactState::Discovered, ArtifactState::Corrupt},
  {ArtifactState::Discovered, ArtifactState::Failed},
  {ArtifactState::Discovered, ArtifactState::Retired},
  {ArtifactState::Discovered, ArtifactState::Terminal},
  {ArtifactState::Building, ArtifactState::Built},
  {ArtifactState::Building, ArtifactState::Failed},
  {ArtifactState::Building, ArtifactState::Corrupt},
  {ArtifactState::Building, ArtifactState::Invalidated},
  {ArtifactState::Building, ArtifactState::Terminal},
  {ArtifactState::Built, ArtifactState::Validating},
  {ArtifactState::Built, ArtifactState::Invalidated},
  {ArtifactState::Built, ArtifactState::Corrupt},
  {ArtifactState::Built, ArtifactState::Retired},
  {ArtifactState::Built, ArtifactState::Failed},
  {ArtifactState::Built, ArtifactState::Terminal},
  {ArtifactState::Validating, ArtifactState::Valid},
  {ArtifactState::Validating, ArtifactState::Failed},
  {ArtifactState::Validating, ArtifactState::Corrupt},
  {ArtifactState::Validating, ArtifactState::Invalidated},
  {ArtifactState::Validating, ArtifactState::Terminal},
  {ArtifactState::Valid, ArtifactState::Loading},
  {ArtifactState::Valid, ArtifactState::Persisted},
  {ArtifactState::Valid, ArtifactState::Leasing},
  {ArtifactState::Valid, ArtifactState::InUse},
  {ArtifactState::Valid, ArtifactState::DemotionPending},
  {ArtifactState::Valid, ArtifactState::EvictionPending},
  {ArtifactState::Valid, ArtifactState::Invalidated},
  {ArtifactState::Valid, ArtifactState::Retired},
  {ArtifactState::Valid, ArtifactState::Corrupt},
  {ArtifactState::Valid, ArtifactState::Terminal},
  {ArtifactState::Loading, ArtifactState::ResidentHost},
  {ArtifactState::Loading, ArtifactState::ResidentDevice},
  {ArtifactState::Loading, ArtifactState::Failed},
  {ArtifactState::Loading, ArtifactState::Corrupt},
  {ArtifactState::Loading, ArtifactState::Invalidated},
  {ArtifactState::Loading, ArtifactState::Terminal},
  {ArtifactState::ResidentHost, ArtifactState::ResidentDevice},
  {ArtifactState::ResidentHost, ArtifactState::Persisted},
  {ArtifactState::ResidentHost, ArtifactState::DemotionPending},
  {ArtifactState::ResidentHost, ArtifactState::EvictionPending},
  {ArtifactState::ResidentHost, ArtifactState::EvictedHost},
  {ArtifactState::ResidentHost, ArtifactState::Invalidated},
  {ArtifactState::ResidentHost, ArtifactState::Corrupt},
  {ArtifactState::ResidentHost, ArtifactState::Retired},
  {ArtifactState::ResidentHost, ArtifactState::Terminal},
  {ArtifactState::ResidentDevice, ArtifactState::ResidentHost},
  {ArtifactState::ResidentDevice, ArtifactState::DemotionPending},
  {ArtifactState::ResidentDevice, ArtifactState::EvictionPending},
  {ArtifactState::ResidentDevice, ArtifactState::EvictedDevice},
  {ArtifactState::ResidentDevice, ArtifactState::Invalidated},
  {ArtifactState::ResidentDevice, ArtifactState::Corrupt},
  {ArtifactState::ResidentDevice, ArtifactState::Retired},
  {ArtifactState::ResidentDevice, ArtifactState::Terminal},
  {ArtifactState::Persisted, ArtifactState::ResidentHost},
  {ArtifactState::Persisted, ArtifactState::Valid},
  {ArtifactState::Persisted, ArtifactState::EvictionPending},
  {ArtifactState::Persisted, ArtifactState::Invalidated},
  {ArtifactState::Persisted, ArtifactState::Corrupt},
  {ArtifactState::Persisted, ArtifactState::Retired},
  {ArtifactState::Persisted, ArtifactState::Terminal},
  {ArtifactState::Leasing, ArtifactState::InUse},
  {ArtifactState::Leasing, ArtifactState::DemotionPending},
  {ArtifactState::Leasing, ArtifactState::Invalidated},
  {ArtifactState::Leasing, ArtifactState::Retired},
  {ArtifactState::Leasing, ArtifactState::Terminal},
  {ArtifactState::InUse, ArtifactState::Leasing},
  {ArtifactState::InUse, ArtifactState::DemotionPending},
  {ArtifactState::InUse, ArtifactState::Invalidated},
  {ArtifactState::InUse, ArtifactState::Retired},
  {ArtifactState::InUse, ArtifactState::Terminal},
  {ArtifactState::DemotionPending, ArtifactState::EvictionPending},
  {ArtifactState::DemotionPending, ArtifactState::Invalidated},
  {ArtifactState::DemotionPending, ArtifactState::Retired},
  {ArtifactState::DemotionPending, ArtifactState::Terminal},
  {ArtifactState::EvictionPending, ArtifactState::EvictedDevice},
  {ArtifactState::EvictionPending, ArtifactState::EvictedHost},
  {ArtifactState::EvictionPending, ArtifactState::Invalidated},
  {ArtifactState::EvictionPending, ArtifactState::Retired},
  {ArtifactState::EvictionPending, ArtifactState::Terminal},
  {ArtifactState::EvictedDevice, ArtifactState::Persisted},
  {ArtifactState::EvictedDevice, ArtifactState::Retired},
  {ArtifactState::EvictedDevice, ArtifactState::Terminal},
  {ArtifactState::EvictedHost, ArtifactState::Persisted},
  {ArtifactState::EvictedHost, ArtifactState::Retired},
  {ArtifactState::EvictedHost, ArtifactState::Terminal},
  {ArtifactState::Invalidated, ArtifactState::Retired},
  {ArtifactState::Invalidated, ArtifactState::Terminal},
  {ArtifactState::Corrupt, ArtifactState::Failed},
  {ArtifactState::Corrupt, ArtifactState::Retired},
  {ArtifactState::Corrupt, ArtifactState::Terminal},
  {ArtifactState::Failed, ArtifactState::Retired},
  {ArtifactState::Failed, ArtifactState::Terminal},
  {ArtifactState::Retired, ArtifactState::Terminal},
};

bool contains(ArtifactState from, ArtifactState to) {
  for (auto& p : kTransitions) if (p.first == from && p.second == to) return true;
  return false;
}
}  // namespace

bool can_transition(ArtifactState from, ArtifactState to) noexcept { return contains(from, to); }

bool is_terminal(ArtifactState s) noexcept {
  return s == ArtifactState::Retired || s == ArtifactState::Terminal ||
         s == ArtifactState::Failed || s == ArtifactState::Corrupt ||
         s == ArtifactState::Invalidated;
}

bool is_poisoned(ArtifactState s) noexcept {
  return s == ArtifactState::Corrupt || s == ArtifactState::Failed ||
         s == ArtifactState::Invalidated || s == ArtifactState::Retired ||
         s == ArtifactState::Terminal;
}

bool is_eligible(ArtifactState s) noexcept {
  return s == ArtifactState::Valid || s == ArtifactState::ResidentHost ||
         s == ArtifactState::ResidentDevice || s == ArtifactState::Persisted ||
         s == ArtifactState::Leasing || s == ArtifactState::InUse;
}

bool can_start(ArtifactState s) noexcept {
  return s == ArtifactState::Discovered || s == ArtifactState::Building;
}

const char* transition_reason(ArtifactState from, ArtifactState to) noexcept {
  if (from == to) return "self";
  if (!contains(from, to)) return "illegal";
  if (to == ArtifactState::Building) return "build requested";
  if (to == ArtifactState::Built) return "compile produced artifact bytes";
  if (to == ArtifactState::Validating) return "validation staged";
  if (to == ArtifactState::Valid) return "validation passed";
  if (to == ArtifactState::ResidentHost) return "host image loaded";
  if (to == ArtifactState::ResidentDevice) return "device module loaded";
  if (to == ArtifactState::Persisted) return "persisted to storage";
  if (to == ArtifactState::Leasing) return "lease acquired";
  if (to == ArtifactState::InUse) return "executing";
  if (to == ArtifactState::DemotionPending) return "demotion scheduled";
  if (to == ArtifactState::EvictionPending) return "eviction scheduled";
  if (to == ArtifactState::EvictedDevice) return "device image evicted";
  if (to == ArtifactState::EvictedHost) return "host image evicted";
  if (to == ArtifactState::Invalidated) return "invalidated";
  if (to == ArtifactState::Corrupt) return "corruption detected";
  if (to == ArtifactState::Failed) return "failure";
  if (to == ArtifactState::Retired) return "retired";
  if (to == ArtifactState::Terminal) return "terminal";
  return "transition";
}

}  // namespace kernelcache
