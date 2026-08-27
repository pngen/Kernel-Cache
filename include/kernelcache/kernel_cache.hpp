// kernel_cache.hpp - the KernelCache public API.
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>

#include "kernelcache/result.hpp"
#include "kernelcache/key.hpp"
#include "kernelcache/compatibility.hpp"
#include "kernelcache/descriptors.hpp"
#include "kernelcache/artifact.hpp"
#include "kernelcache/backend.hpp"
#include "kernelcache/lookup.hpp"
#include "kernelcache/invalidation.hpp"
#include "kernelcache/residency.hpp"
#include "kernelcache/persistence.hpp"
#include "kernelcache/stats.hpp"
#include "kernelcache/explain.hpp"
#include "kernelcache/clock.hpp"

namespace kernelcache {

// ---------------------------------------------------------------------------
// KernelCacheConfig
// ---------------------------------------------------------------------------
struct KernelCacheConfig {
  std::string namespace_;                 // default namespace
  std::string persistence_root;           // empty => no persistence
  bool persistence_enabled = true;        // effective only if root non-empty
  ResidencyPolicy residency;
  CompatibilityPolicy compatibility;
  std::uint64_t max_artifacts = 1u << 20;
  std::uint64_t max_build_concurrency = 8;
  std::uint64_t event_history_cap = 4096;
  std::size_t min_cache_ms = 0;           // (unused placeholder retained for API stability)

  // Scalar cost model (ms / ns) used by cost-aware scoring when measured data
  // is unavailable; labeled as configured, not measured.
  std::uint64_t configured_compile_ms = 100;
  std::uint64_t configured_reload_ms = 5;
  bool configured_marks = true;
};

// ---------------------------------------------------------------------------
// KernelCache
// ---------------------------------------------------------------------------
class KernelCache {
 public:
  struct Impl;

  explicit KernelCache(KernelCacheConfig config = {});
  ~KernelCache();

  KernelCache(const KernelCache&) = delete;
  KernelCache& operator=(const KernelCache&) = delete;

  // --- configuration -------------------------------------------------------
  const KernelCacheConfig& config() const noexcept;
  const KernelCompatibilityPolicy& compatibility_policy() const noexcept;
  const ResidencyPolicy& residency_policy() const noexcept;

  // Backend registration (CPU and CUDA backends are registered if built-in).
  Result<void> register_backend(KernelBackend backend);
  Result<void> use_builtin_backends();

  // --- primary lookup/lease pipeline ---------------------------------------
  // Normalize the request, build the typed identity, query candidates, apply the
  // policy, validate generation/state, acquire a lease, promote/load and return
  // an execution-ready artifact handle.
  Result<KernelLease> acquire(const KernelLookupRequest& request);
  // Non-owning lookup (no lease). Returns a structured hit/miss result.
  Result<KernelLookupResult> lookup(const KernelLookupRequest& request);

  // --- build ---------------------------------------------------------------
  // Explicit build for a request. Real compilation through the backend.
  Result<ArtifactDescriptor> build(const KernelLookupRequest& request);
  // Publish a backend-produced artifact under a finalized key.
  Result<ArtifactId> put(ArtifactDescriptor desc, std::vector<std::uint8_t> bytes);

  // --- validate / load / unload --------------------------------------------
  Result<ValidationDescriptor> validate(const KernelLookupRequest& request,
                                        const ArtifactId& artifact);
  Result<ArtifactHandle> load(const ArtifactId& artifact);
  Result<void> unload(const ArtifactId& artifact);
  Result<void> execute(const ArtifactHandle& handle, void* args, std::size_t arg_bytes);

  // --- invalidation --------------------------------------------------------
  Result<std::vector<InvalidationOutcome>> invalidate(const InvalidationRequest& request);

  // --- residency -----------------------------------------------------------
  Result<void> evict(const ArtifactId& artifact, bool force = false);
  Result<void> pin(const ArtifactId& artifact);
  Result<void> unpin(const ArtifactId& artifact);
  Result<std::vector<EvictionCandidate>> eviction_candidates(std::uint32_t top_n = 16);

  // --- introspection -------------------------------------------------------
  Result<std::vector<ArtifactDescriptor>> list(
      std::optional<std::string> namespace_ = std::nullopt,
      std::optional<ArtifactState> state = std::nullopt) const;
  Result<ArtifactDescriptor> inspect(const ArtifactId& artifact) const;
  Stats stats() const;
  Snapshot snapshot() const;
  EventHistory& events();
  Explain explain(const ArtifactId& artifact) const;
  Explain explain(const KernelLookupRequest& request) const;
  void reset_stats();

  // --- recovery ------------------------------------------------------------
  Result<std::size_t> recover(std::vector<std::string>* rejected = nullptr,
                              std::vector<std::string>* orphans = nullptr);

  // --- generation authority ------------------------------------------------
  CacheGeneration current_cache_generation() const noexcept;
  Result<CacheGeneration> roll_cache_generation();

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace kernelcache
