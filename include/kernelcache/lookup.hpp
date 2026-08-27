// lookup.hpp - lookup requests/results, leases and reservations.
#pragma once

#include <memory>
#include <string>

#include "kernelcache/key.hpp"
#include "kernelcache/result.hpp"
#include "kernelcache/compatibility.hpp"
#include "kernelcache/descriptors.hpp"
#include "kernelcache/backend.hpp"

namespace kernelcache {

// Structured lookup outcome class.
enum class LookupOutcome : std::uint8_t {
  ExactHit = 0,
  CompatibleHit = 1,
  HostResidentHit = 2,
  DeviceResidentHit = 3,
  PersistedHit = 4,
  MissRequiresBuild = 5,
  MissIncompatibility = 6,
  MissInvalidated = 7,
  MissCorrupt = 8,
  MissResidencyPressure = 9,
  MissPolicy = 10,
};

const char* lookup_outcome_name(LookupOutcome o) noexcept;
std::string to_string(LookupOutcome o);

// A lookup request. It is normalized by the cache into a finalized key.
struct KernelLookupRequest {
  KernelCompatibilityKey key;         // may be pre-finalized or not
  DeviceDescriptor device;            // target device for load/execute
  std::string namespace_;
  ResidencyTier desired_tier = ResidencyTier::HostResident;
  CompatibilityPolicy policy;
  bool allow_miss_build = false;       // if false, a miss returns MissRequiresBuild without building
  bool force_validate = false;         // re-validate eligible artifact before returning
  bool pin = false;                    // pin for the duration of the lease
  std::uint32_t alignment_hint = 0;
  std::string source;                 // kernel source text (CUDA) / empty for synthetic CPU
};

// A structured lookup result. Always carries the decision and a human-readable
// explanation, even on miss.
struct KernelLookupResult {
  LookupOutcome outcome = LookupOutcome::MissRequiresBuild;
  std::optional<ArtifactId> artifact;
  std::optional<ArtifactGeneration> generation;
  KernelCompatibilityDecision decision;
  std::vector<std::string> notes;
  std::uint64_t lookup_ns = 0;
  bool from_cache = false;

  bool is_hit() const noexcept {
    return outcome == LookupOutcome::ExactHit || outcome == LookupOutcome::CompatibleHit ||
           outcome == LookupOutcome::HostResidentHit || outcome == LookupOutcome::DeviceResidentHit ||
           outcome == LookupOutcome::PersistedHit;
  }
  std::string summary() const;
};

// ---------------------------------------------------------------------------
// KernelLease
// ---------------------------------------------------------------------------
// A lease pins eligibility for the duration of use. It deadlock-free, move-only
// and idempotently released. It never keeps an invalidated artifact reusable.
class KernelLease {
 public:
  KernelLease() = default;
  KernelLease(const KernelLease&) = delete;
  KernelLease& operator=(const KernelLease&) = delete;
  KernelLease(KernelLease&& other) noexcept;
  KernelLease& operator=(KernelLease&& other) noexcept;
  ~KernelLease();

  bool valid() const noexcept;
  ArtifactId artifact_id() const;
  ArtifactGeneration generation() const;
  ResidencyTier residency() const;
  LoadedModuleHandle native_handle() const;
  const KernelCompatibilityKey& key() const;
  // Idempotent release. Safe to call multiple times.
  void release();
  // Execute the loaded kernel through the owning backend loader. Returns false
  // when no execution path is bound (CPU case returns the reference result).
  Result<void> touch();

 private:
  friend class KernelCache;
  struct State;  // pimpl
  std::shared_ptr<State> state_;
};

// ---------------------------------------------------------------------------
// KernelReservation
// ---------------------------------------------------------------------------
// A reservation is a lightweight hold that prevents eviction of the underlying
// slot for a restricted scope (used by the single-flight build reservation).
class KernelReservation {
 public:
  KernelReservation() = default;
  KernelReservation(KernelReservation&&) noexcept;
  KernelReservation& operator=(KernelReservation&&) noexcept;
  ~KernelReservation();
  bool valid() const noexcept;
  void release();

 private:
  friend class KernelCache;
  struct State;
  std::shared_ptr<State> state_;
};

}  // namespace kernelcache