// impl.hpp - internal KernelCache::Impl declaration.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "kernelcache/kernel_cache.hpp"
#include "src/internal.hpp"

namespace kernelcache {

namespace internal {
struct CandidateSnapshot;
}

// ---------------------------------------------------------------------------
// KernelCache::Impl - owns all internal state and coordination.
// ---------------------------------------------------------------------------
struct KernelCache::Impl {
  KernelCacheConfig cfg;
  std::unique_ptr<Clock> clock = std::make_unique<SystemClock>();

  // Master state lock: guards by_id, indices, per-recordable mutable state.
  mutable std::shared_mutex mtx_;
  std::unordered_map<ArtifactId, std::shared_ptr<internal::ArtifactRecord>> by_id;
  std::unordered_map<KernelCompatibilityKey, std::vector<ArtifactId>> by_key;
  std::unordered_map<std::string, std::vector<ArtifactId>> by_operation;
  CacheGeneration cache_generation{1};
  ArtifactGeneration next_generation{0};

  // Build coordination (single-flight). Backend builds never run while this or
  // mtx_ is held.
  std::mutex build_mtx_;
  std::condition_variable build_cv_;
  std::unordered_map<KernelCompatibilityKey, std::shared_ptr<internal::BuildFence>> in_flight;

  // Backends registered by name.
  std::unordered_map<std::string, std::shared_ptr<KernelBackend>> backends;

  // Persistence.
  std::unique_ptr<PersistenceStore> persistence;
  bool persistence_open = false;

  // Stats + events (own lock; never acquired while mtx_ is held).
  mutable std::mutex stats_mtx_;
  Stats stats_;
  EventHistory events_;

  Impl();
  ~Impl();

  // --- small helpers -------------------------------------------------------
  void record_event(std::string type, std::string message);
  void bump(std::function<void(Stats&)> f);
  std::shared_ptr<KernelBackend> find_backend(const KernelCompatibilityKey& key) const;
  void insert_index_locked(const std::shared_ptr<internal::ArtifactRecord>& r);
  void remove_index_locked(const std::shared_ptr<internal::ArtifactRecord>& r);

  // --- pipeline ------------------------------------------------------------
  Result<KernelLookupResult> lookup_impl(KernelLookupRequest request);
  Result<KernelLease> acquire_impl(KernelLookupRequest request);
  Result<ArtifactDescriptor> build_impl(KernelLookupRequest request, bool* build_toward);
  std::vector<internal::CandidateSnapshot> collect_candidates(const KernelCompatibilityKey& request) const;
  std::shared_ptr<internal::ArtifactRecord> make_record(
      const ArtifactDescriptor& desc, std::vector<std::uint8_t> bytes);
  Result<void> promote_to_tier(const std::shared_ptr<internal::ArtifactRecord>& rec,
                               ResidencyTier desired, const DeviceDescriptor& device);
  Result<std::vector<std::uint8_t>> ensure_bytes(const std::shared_ptr<internal::ArtifactRecord>& rec);
  void evict_to_fit(ResidencyTier tier);
  Result<KernelLease> grant_lease(const std::shared_ptr<internal::ArtifactRecord>& rec,
                                  KernelLookupRequest& request);
  void on_lease_release(const std::shared_ptr<internal::ArtifactRecord>& rec,
                        std::uint64_t load_gen_value);

  // --- management ----------------------------------------------------------
  Result<std::vector<InvalidationOutcome>> invalidate_impl(const InvalidationRequest& req);
  Result<void> evict_impl(const ArtifactId& id, bool force);
  Result<void> pin_impl(const ArtifactId& id);
  Result<void> unpin_impl(const ArtifactId& id);
  std::pair<ArtifactId, std::string> pick_eviction_locked(ResidencyTier tier);
  Result<std::vector<EvictionCandidate>> eviction_candidates_impl(std::uint32_t top_n);
  std::size_t enforce_budgets_locked();
  Result<std::vector<ArtifactDescriptor>> list_impl(std::optional<std::string> ns,
                                                    std::optional<ArtifactState> state) const;
  Result<ArtifactDescriptor> inspect_impl(const ArtifactId& id) const;
  Snapshot snapshot_impl() const;
  Explain explain_impl(const ArtifactId& id) const;
  Result<std::size_t> recover_impl(std::vector<std::string>* rejected,
                                   std::vector<std::string>* orphans);

  // Build unit (in build.cpp).
  internal::BuildFence::Outcome run_build(KernelLookupRequest request,
                                          ArtifactGeneration target_gen,
                                          BuildAttemptId attempt);
};

}  // namespace kernelcache