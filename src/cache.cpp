#include "kernelcache/kernel_cache.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <sstream>
#include <thread>
#include <cstdlib>

#include "src/impl.hpp"
#include "src/internal.hpp"
#include "kernelcache/lifecycle.hpp"
#include "kernelcache/sha256.hpp"

namespace kernelcache {

using internal::ArtifactRecord;
using internal::BuildFence;
using internal::CandidateSnapshot;

namespace {
ArtifactId derive_artifact_id(const std::vector<std::uint8_t>& bytes, ArtifactGeneration gen) {
  Sha256Digest d = Sha256::digest(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
  std::uint64_t lo = 0;
  for (int i = 0; i < 8; ++i) lo = (lo << 8) | d[i];
  return ArtifactId(gen.value, lo);
}
std::uint64_t now_ns() {
  auto t = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t).count());
}
}  // namespace

// ---------------------------------------------------------------------------
// Impl lifecycle
// ---------------------------------------------------------------------------
KernelCache::Impl::Impl() : events_(4096) {}
KernelCache::Impl::~Impl() = default;

void KernelCache::Impl::record_event(std::string type, std::string message) {
  Event e; e.time_ns = clock->now_ns(); e.type = std::move(type); e.message = std::move(message);
  std::lock_guard<std::mutex> lk(stats_mtx_);
  events_.record(std::move(e));
}

void KernelCache::Impl::bump(std::function<void(Stats&)> f) {
  std::lock_guard<std::mutex> lk(stats_mtx_);
  f(stats_);
}

std::shared_ptr<KernelBackend> KernelCache::Impl::find_backend(const KernelCompatibilityKey& key) const {
  auto it = backends.find(key.runtime_backend);
  if (it != backends.end()) return it->second;
  for (auto& [name, b] : backends) { if (b->builder && b->builder->can_build(key)) return b; }
  return nullptr;
}

void KernelCache::Impl::insert_index_locked(const std::shared_ptr<ArtifactRecord>& r) {
  by_id[r->id] = r;
  by_key[r->key].push_back(r->id);
  by_operation[r->operation].push_back(r->id);
}
void KernelCache::Impl::remove_index_locked(const std::shared_ptr<ArtifactRecord>& r) {
  by_id.erase(r->id);
  auto& kv = by_key[r->key];
  kv.erase(std::remove(kv.begin(), kv.end(), r->id), kv.end());
  if (kv.empty()) by_key.erase(r->key);
  auto& ov = by_operation[r->operation];
  ov.erase(std::remove(ov.begin(), ov.end(), r->id), ov.end());
  if (ov.empty()) by_operation.erase(r->operation);
}

std::shared_ptr<ArtifactRecord> KernelCache::Impl::make_record(const ArtifactDescriptor& desc,
                                                              std::vector<std::uint8_t> bytes) {
  auto rec = std::make_shared<ArtifactRecord>();
  rec->id = desc.id;
  rec->generation = desc.generation;
  rec->key = desc.key;
  rec->desc = desc;
  rec->bytes = std::move(bytes);
  rec->state = ArtifactState::Valid;
  rec->tier = ResidencyTier::MetadataOnly;
  rec->persistent = false;
  rec->validated = desc.validation.all_passed();
  rec->tenant = desc.namespace_;
  rec->operation = desc.key.operation;
  rec->arch = desc.key.arch;
  rec->backend = desc.backend;
  rec->desc.namespace_ = desc.namespace_;
  {
    const Sha256Digest& d = rec->key.digest();
    std::uint64_t hi = 0, lo = 0;
    for (int i = 0; i < 8; ++i) { hi = (hi << 8) | d[i]; lo = (lo << 8) | d[8 + i]; }
    rec->kernel_id = KernelId(hi, lo);
  }
  rec->last_access_ns = now_ns();
  return rec;
}

// ---------------------------------------------------------------------------
// Candidate gathering (under shared lock; decision evaluated outside).
// ---------------------------------------------------------------------------
std::vector<CandidateSnapshot> KernelCache::Impl::collect_candidates(const KernelCompatibilityKey& request) const {
  std::vector<CandidateSnapshot> out;
  std::shared_lock<std::shared_mutex> lk(mtx_);
  auto emit = [&](const std::shared_ptr<ArtifactRecord>& r) {
    CandidateSnapshot c;
    c.rec = r;
    c.key = r->key;
    c.gen = r->generation;
    c.state = r->state;
    c.tier = r->tier;
    c.persistent = r->persistent;
    c.validated = r->validated;
    c.invalidated = r->invalidated_;
    c.corrupt = r->corrupt_;
    c.operation = r->operation;
    c.backend = r->backend;
    out.push_back(std::move(c));
  };
  // Exact-first: artifacts indexed by the exact canonical key digest.
  auto it = by_key.find(request);
  if (it != by_key.end()) for (auto& id : it->second) {
    auto rit = by_id.find(id);
    if (rit != by_id.end()) emit(rit->second);
  }
  // Compatible scan across same operation when no exact key exists.
  if (out.empty() && !request.operation.empty()) {
    auto oit = by_operation.find(request.operation);
    if (oit != by_operation.end()) for (auto& id : oit->second) {
      auto rit = by_id.find(id);
      if (rit != by_id.end()) emit(rit->second);
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Lookup (decision only; no lease, no promotion).
// ---------------------------------------------------------------------------
Result<KernelLookupResult> KernelCache::Impl::lookup_impl(KernelLookupRequest request) {
  KernelLookupResult result;
  std::uint64_t t0 = now_ns();
  request.key.finalize();
  if (!request.key.finalized()) return Result<KernelLookupResult>(ErrorCode::InvalidArgument, "key not finalizable");
  if (request.namespace_.empty()) request.namespace_ = cfg.namespace_;

  auto candidates = collect_candidates(request.key);
  // Evaluate compatibility outside the lock.
  std::vector<CandidateSnapshot> compatible;
  for (auto& c : candidates) {
    if (c.invalidated || c.corrupt || !c.validated) continue;
    if (!kernelcache::is_eligible(c.state)) continue;
    c.decision = evaluate_compatibility(request.key, c.key, request.policy);
    if (c.decision.is_compatible()) compatible.push_back(c);
  }
  // Pick the best (smallest distance = most exact).
  auto best = std::min_element(compatible.begin(), compatible.end(),
      [](const CandidateSnapshot& a, const CandidateSnapshot& b) {
        return compatibility_distance(a.decision) < compatibility_distance(b.decision);
      });
  result.lookup_ns = now_ns() - t0;

  if (best == compatible.end()) {
    // No compatible candidate. Distinguish "all poisoned" from "no healthy but
    // incompatible" so the caller can explain why it missed.
    bool any_candidate = !candidates.empty();
    bool any_healthy = false;
    for (auto& c : candidates) if (!c.invalidated && !c.corrupt && c.validated) any_healthy = true;
    if (any_candidate && !any_healthy) { result.outcome = LookupOutcome::MissInvalidated; result.notes.push_back("all candidates invalidated/corrupt"); }
    else result.outcome = LookupOutcome::MissRequiresBuild;
    result.notes.push_back("no compatible eligible candidate");
    bump([&](Stats& s){ s.lookups++; s.misses++; });
    record_event("lookup", std::string("miss ") + lookup_outcome_name(result.outcome) + " op=" + request.key.operation);
    return result;
  }

  const CandidateSnapshot& chosen = *best;
  result.decision = chosen.decision;
  result.artifact = chosen.rec->id;
  result.generation = chosen.gen;
  result.from_cache = true;

  // Classify residency-specific outcome first, then the compatibility class.
  LookupOutcome kind;
  switch (chosen.tier) {
    case ResidencyTier::DeviceResident: kind = LookupOutcome::DeviceResidentHit; break;
    case ResidencyTier::HostResident: kind = LookupOutcome::HostResidentHit; break;
    case ResidencyTier::PersistentStorage: kind = LookupOutcome::PersistedHit; break;
    default: kind = chosen.decision.is_exact() ? LookupOutcome::ExactHit : LookupOutcome::CompatibleHit; break;
  }
  result.outcome = kind;
  result.notes.push_back(chosen.decision.summary());
  bump([&](Stats& s){
    s.lookups++;
    if (kind == LookupOutcome::ExactHit) s.exact_hits++;
    else if (kind == LookupOutcome::CompatibleHit) s.compatible_hits++;
    else if (kind == LookupOutcome::PersistedHit) s.persistent_hits++;
    else if (kind == LookupOutcome::HostResidentHit) s.host_hits++;
    else if (kind == LookupOutcome::DeviceResidentHit) s.device_hits++;
  });
  record_event("lookup", std::string("hit ") + lookup_outcome_name(kind) + " op=" + request.key.operation);
  return result;
}
// ---------------------------------------------------------------------------
// Single-flight build. Exactly one owner compiles; waiters observe the shared
// future. The backend build/validate runs outside every cache lock.
// ---------------------------------------------------------------------------
Result<ArtifactDescriptor> KernelCache::Impl::build_impl(KernelLookupRequest request, bool* is_owner) {
  request.key.finalize();
  if (request.namespace_.empty()) request.namespace_ = cfg.namespace_;
  std::shared_ptr<BuildFence> fence;
  ArtifactGeneration target_gen{0};
  BuildAttemptId attempt;
  bool owner = false;
  {
    std::lock_guard<std::mutex> lk(build_mtx_);
    auto it = in_flight.find(request.key);
    if (it != in_flight.end()) {
      fence = it->second;
      bump([&](Stats& s){ s.build_dedup++; });
    } else {
      owner = true;
      target_gen = ArtifactGeneration(next_generation.value++);
      attempt = BuildAttemptId(now_ns(), static_cast<std::uint64_t>(std::rand()));
      fence = std::make_shared<BuildFence>();
      fence->attempt = attempt;
      fence->build_generation = target_gen;
      fence->started_ns = now_ns();
      fence->future = fence->promise.get_future().share();
      in_flight.emplace(request.key, fence);
      bump([&](Stats& s){ ++s.active_builds; ++s.builds; });
      record_event("build", "start attempt=" + attempt.str());
    }
  }
  if (owner) {
    auto outc = run_build(request, target_gen, attempt);
    bump([&](Stats& s){ if (s.active_builds > 0) --s.active_builds; });
    fence->promise.set_value(outc);
    {
      std::lock_guard<std::mutex> lk(build_mtx_);
      auto it2 = in_flight.find(request.key);
      if (it2 != in_flight.end() && it2->second == fence) in_flight.erase(it2);
    }
    if (is_owner) *is_owner = true;
    if (!outc.succeeded) {
      bump([&](Stats& s){ ++s.failed_builds; });
      record_event("build", "failed attempt=" + attempt.str() + " " + outc.error);
      return Result<ArtifactDescriptor>(outc.error_code, outc.error);
    }
    record_event("build", "succeeded attempt=" + attempt.str() + " id=" + outc.id.str());
    bool new_record = false;
    {
      std::lock_guard<std::shared_mutex> lk(mtx_);
      if (!by_id.count(outc.id)) {
        auto rec = make_record(outc.desc, outc.bytes);
        rec->desc = outc.desc;
        insert_index_locked(rec);
        new_record = true;
      }
    }
    if (new_record && persistence_open && persistence) {
      // Persist outside the master lock.
      StoredArtifact sa;
      sa.id = outc.desc.id;
      sa.generation = outc.desc.generation;
      sa.key = outc.desc.key;
      sa.bytes = outc.bytes;
      sa.sizes = outc.desc.sizes;
      sa.provenance = outc.desc.provenance;
      sa.format = outc.desc.format;
      sa.sha256_hex = outc.desc.bytes_sha256;
      sa.namespace_ = outc.desc.namespace_;
      sa.stored_ns = now_ns();
      auto pr = persistence->put(sa);
      if (pr) {
        std::lock_guard<std::shared_mutex> lk(mtx_);
        auto rit = by_id.find(outc.id);
        if (rit != by_id.end()) { rit->second->persistent = true; rit->second->tier = ResidencyTier::PersistentStorage; }
        bump([&](Stats& s){ s.bytes_persisted += sa.bytes.size(); });
      } else {
        std::lock_guard<std::mutex> lk(stats_mtx_);
        record_event("persist", "failed " + pr.error().message());
      }
    }
    return outc.desc;
  } else {
    auto outc = fence->future.get();
    if (is_owner) *is_owner = false;
    if (!outc.succeeded) return Result<ArtifactDescriptor>(outc.error_code, "waiter observed failed build: " + outc.error);
    std::shared_lock<std::shared_mutex> lk(mtx_);
    auto rit = by_id.find(outc.id);
    if (rit == by_id.end()) return Result<ArtifactDescriptor>(ErrorCode::NotFound, "built artifact not in index");
    return rit->second->desc;
  }
}

// ---------------------------------------------------------------------------
// Acquire (lease). Looks up, promotes to the desired tier, and returns a lease.
// ---------------------------------------------------------------------------
Result<KernelLease> KernelCache::Impl::acquire_impl(KernelLookupRequest request) {
  request.key.finalize();
  auto decide = lookup_impl(request);
  if (!decide) return Result<KernelLease>(decide.error().code(), decide.error().message());
  if (decide.value().is_hit()) {
    auto rec = [&]() -> std::shared_ptr<ArtifactRecord> {
      std::shared_lock<std::shared_mutex> lk(mtx_);
      auto it = by_id.find(*decide.value().artifact);
      return it != by_id.end() ? it->second : nullptr;
    }();
    if (!rec) return Result<KernelLease>(ErrorCode::NotFound, "artifact vanished");
    auto pr = promote_to_tier(rec, request.desired_tier, request.device);
    if (!pr) return Result<KernelLease>(pr.error().code(), pr.error().message());
    return grant_lease(rec, request);
  }
  // Miss.
  if (request.allow_miss_build) {
    auto b = build_impl(request, nullptr);
    if (!b) return Result<KernelLease>(b.error().code(), b.error().message());
    auto rec = [&]() -> std::shared_ptr<ArtifactRecord> {
      std::lock_guard<std::shared_mutex> lk(mtx_);
      auto it = by_id.find(b.value().id);
      return it != by_id.end() ? it->second : nullptr;
    }();
    if (!rec) return Result<KernelLease>(ErrorCode::NotFound, "built artifact vanished");
    auto pr = promote_to_tier(rec, request.desired_tier, request.device);
    if (!pr) return Result<KernelLease>(pr.error().code(), pr.error().message());
    return grant_lease(rec, request);
  }
  std::string msg = "miss: " + std::string(lookup_outcome_name(decide.value().outcome)) +
                    " op=" + request.key.operation;
  return Result<KernelLease>(ErrorCode::NotFound, msg);
}

// ---------------------------------------------------------------------------
// Release accounting: idempotent, never underflows.
// ---------------------------------------------------------------------------
void KernelCache::Impl::on_lease_release(const std::shared_ptr<ArtifactRecord>& rec,
                                         std::uint64_t load_gen_value) {
  (void)load_gen_value;
  std::lock_guard<std::shared_mutex> lk(mtx_);
  if (rec->lease_count > 0) --rec->lease_count;
  if (rec->lease_count == 0 && (rec->invalidated_ || rec->corrupt_)) {
    if (can_transition(rec->state, ArtifactState::Retired)) rec->state = ArtifactState::Retired;
  }
  bump([&](Stats& s){ if (s.active_leases > 0) --s.active_leases; });
}
// ---------------------------------------------------------------------------
// Management: invalidation, eviction, pinning, listing, snapshot, explain.
// ---------------------------------------------------------------------------
Result<std::vector<InvalidationOutcome>> KernelCache::Impl::invalidate_impl(const InvalidationRequest& req) {
  // Guard: hard invalidation of actively-leased artifacts is forbidden to avoid
  // breaking an executing handle; it is recognized but demoted to a drain.
  std::vector<InvalidationOutcome> outcomes;
  std::vector<ArtifactId> target_ids;
  {
    std::lock_guard<std::shared_mutex> lk(mtx_);
    for (auto& [id, rec] : by_id) {
      bool match = false;
      switch (req.target) {
        case InvalidationTarget::ArtifactId:
          match = (id == ArtifactId(req.artifact_hi.value_or(0), req.artifact_lo.value_or(0)));
          break;
        case InvalidationTarget::KernelId:
          match = (rec->kernel_id == KernelId(req.kernel_hi.value_or(0), req.kernel_lo.value_or(0)));
          break;
        case InvalidationTarget::CompatibilityKey:
          match = req.key && (rec->key == *req.key);
          break;
        case InvalidationTarget::Operation:
          match = (!req.operation.empty() && rec->operation == req.operation);
          break;
        case InvalidationTarget::Architecture:
          match = (!req.architecture.empty() && rec->arch == req.architecture);
          break;
        case InvalidationTarget::CompilerGeneration:
          match = (!req.compiler_generation.empty() &&
                   (rec->desc.compilation.compiler.name == req.compiler_generation ||
                    rec->key.compiler_name == req.compiler_generation));
          break;
        case InvalidationTarget::RuntimeGeneration:
          match = (!req.runtime_generation.empty() && rec->key.runtime_backend == req.runtime_generation);
          break;
        case InvalidationTarget::ModelRevision:
          match = (!req.model_revision.empty() && rec->key.model_revision == req.model_revision);
          break;
        case InvalidationTarget::OperatorRevision:
          match = (!req.operator_revision.empty() && rec->key.operator_revision == req.operator_revision);
          break;
        case InvalidationTarget::Namespace:
          match = (!req.namespace_.empty() && rec->tenant == req.namespace_);
          break;
        case InvalidationTarget::All:
          match = true;
          break;
      }
      if (match) target_ids.push_back(id);
    }
    for (auto& id : target_ids) {
      auto it = by_id.find(id);
      if (it == by_id.end()) continue;
      auto& rec = it->second;
      if (rec->invalidated_) continue;
      rec->invalidated_ = true;
      if (rec->state != ArtifactState::Corrupt && rec->state != ArtifactState::Failed &&
          rec->state != ArtifactState::Retired && rec->state != ArtifactState::Terminal) {
        if (can_transition(rec->state, ArtifactState::Invalidated)) rec->state = ArtifactState::Invalidated;
      }
      // Block new leases immediately. Active leases drain; when the last drains,
      // on_lease_release retires the record.
      InvalidationOutcome o; o.target = req.target; o.applied = true; o.hard = req.hard;
      o.affected_ids.push_back(id); o.affected = 1;
      o.message = "invalidated (lease_count=" + std::to_string(rec->lease_count) + ")";
      outcomes.push_back(std::move(o));
    }
    bump([&](Stats& s){ s.invalidations += static_cast<std::uint64_t>(target_ids.size()); });
  }
  record_event("invalidate", "by=" + std::string(invalidation_target_name(req.target)) + " count=" + std::to_string(target_ids.size()));
  return outcomes;
}

Result<void> KernelCache::Impl::evict_impl(const ArtifactId& id, bool force) {
  std::shared_ptr<ArtifactRecord> rec;
  ArtifactState before;
  {
    std::lock_guard<std::shared_mutex> lk(mtx_);
    auto it = by_id.find(id);
    if (it == by_id.end()) return Result<void>(ErrorCode::NotFound, "artifact not found");
    rec = it->second;
    if (rec->lease_count > 0 || rec->pin_count > 0) {
      if (!force) return Result<void>(ErrorCode::EvictionForbidden, "leased or pinned artifact");
      return Result<void>(ErrorCode::EvictionForbidden, "cannot evict an actively leased/pinned artifact");
    }
    if (rec->invalidated_) { /* already terminal-ish */ }
    before = rec->state;
    if (can_transition(before, ArtifactState::EvictionPending)) rec->state = ArtifactState::EvictionPending;
  }
  // Unload outside the master lock.
  auto ld = find_backend(rec->key);
  if (ld && ld->loader && rec->native_handle) {
    (void)ld->loader->unload(rec->native_handle);
  }
  {
    std::lock_guard<std::shared_mutex> lk(mtx_);
    rec->native_handle.reset();
    rec->device_footprint = 0;
    // Demote residency: if device-resident, evict device; else evict host bytes.
    if (rec->tier == ResidencyTier::DeviceResident) {
      if (can_transition(rec->state, ArtifactState::EvictedDevice)) rec->state = ArtifactState::EvictedDevice;
      rec->tier = rec->persistent ? ResidencyTier::PersistentStorage : ResidencyTier::MetadataOnly;
    } else if (rec->tier == ResidencyTier::HostResident) {
      if (can_transition(rec->state, ArtifactState::EvictedHost)) rec->state = ArtifactState::EvictedHost;
      rec->tier = rec->persistent ? ResidencyTier::PersistentStorage : ResidencyTier::MetadataOnly;
      // Free host bytes (persistent copy remains on disk).
      std::vector<std::uint8_t>().swap(rec->bytes);
      rec->desc.sizes.host_bytes = 0;
    } else {
      rec->tier = rec->persistent ? ResidencyTier::PersistentStorage : ResidencyTier::MetadataOnly;
    }
    bump([&](Stats& s){ s.evictions++; });
  }
  record_event("evict", "id=" + id.str());
  return Result<void>();
}

Result<void> KernelCache::Impl::pin_impl(const ArtifactId& id) {
  std::lock_guard<std::shared_mutex> lk(mtx_);
  auto it = by_id.find(id);
  if (it == by_id.end()) return Result<void>(ErrorCode::NotFound, "artifact not found");
  it->second->pin_count++;
  return Result<void>();
}
Result<void> KernelCache::Impl::unpin_impl(const ArtifactId& id) {
  std::lock_guard<std::shared_mutex> lk(mtx_);
  auto it = by_id.find(id);
  if (it == by_id.end()) return Result<void>(ErrorCode::NotFound, "artifact not found");
  if (it->second->pin_count > 0) it->second->pin_count--;
  return Result<void>();
}

Result<std::vector<ArtifactDescriptor>> KernelCache::Impl::list_impl(std::optional<std::string> ns,
                                                                    std::optional<ArtifactState> state) const {
  std::vector<ArtifactDescriptor> out;
  std::lock_guard<std::shared_mutex> lk(mtx_);
  for (auto& [id, rec] : by_id) {
    if (ns && rec->tenant != *ns) continue;
    if (state && rec->state != *state) continue;
    out.push_back(rec->desc);
  }
  std::sort(out.begin(), out.end(), [](const ArtifactDescriptor& a, const ArtifactDescriptor& b) {
    return a.generation.value < b.generation.value;
  });
  return out;
}

Result<ArtifactDescriptor> KernelCache::Impl::inspect_impl(const ArtifactId& id) const {
  std::lock_guard<std::shared_mutex> lk(mtx_);
  auto it = by_id.find(id);
  if (it == by_id.end()) return Result<ArtifactDescriptor>(ErrorCode::NotFound, "artifact not found");
  return it->second->desc;
}

Snapshot KernelCache::Impl::snapshot_impl() const {
  Snapshot sn;
  std::lock_guard<std::shared_mutex> lk(mtx_);
  sn.artifact_count = by_id.size();
  sn.cache_generation = cache_generation.value;
  for (auto& [id, rec] : by_id) {
    int st = static_cast<int>(rec->state);
    if (st >= 0 && st < 20) sn.artifact_count_by_state[st]++;
    switch (rec->tier) {
      case ResidencyTier::MetadataOnly: sn.metadata_only_bytes += rec->desc.sizes.artifact_bytes; break;
      case ResidencyTier::PersistentStorage: sn.persistent_bytes += rec->desc.sizes.storage_bytes; break;
      case ResidencyTier::HostResident: sn.host_resident_bytes += rec->desc.sizes.host_bytes; break;
      case ResidencyTier::DeviceResident:
        sn.device_resident_bytes += rec->device_footprint;
        if (rec->device_footprint_estimated) sn.device_bytes_estimated = true;
        break;
      default: break;
    }
  }
  std::lock_guard<std::mutex> slk(stats_mtx_);
  sn.stats = stats_;
  return sn;
}

Explain KernelCache::Impl::explain_impl(const ArtifactId& id) const {
  Explain ex; ex.ok = true;
  std::shared_ptr<ArtifactRecord> rec;
  {
    std::shared_lock<std::shared_mutex> lk(mtx_);
    auto it = by_id.find(id);
    if (it == by_id.end()) { ex.ok = false; ex.text = "artifact not found: " + id.str(); ex.json = "{\"ok\":false}"; return ex; }
    rec = it->second;
    ExplainNode n;
    n.topic = "artifact";
    n.summary = rec->id.str() + " gen=" + std::to_string(rec->generation.value);
    n.details.push_back(std::string("state=") + artifact_state_name(rec->state));
    n.details.push_back(std::string("tier=") + residency_tier_name(rec->tier));
    n.details.push_back(std::string("op=") + rec->operation);
    n.details.push_back(std::string("arch=") + rec->arch);
    n.details.push_back("lease_count=" + std::to_string(rec->lease_count));
    n.details.push_back("pin_count=" + std::to_string(rec->pin_count));
    n.details.push_back("reuse=" + std::to_string(rec->reuse_count));
    n.details.push_back("key=" + key_summary(rec->key));
    ex.nodes.push_back(std::move(n));
  }
  std::ostringstream os;
  os << "Artifact " << rec->id.str() << " gen=" << rec->generation.value
     << " state=" << artifact_state_name(rec->state)
     << " tier=" << residency_tier_name(rec->tier)
     << " op=" << rec->operation << " arch=" << rec->arch
     << " leases=" << rec->lease_count << " pins=" << rec->pin_count
     << " reuse=" << rec->reuse_count;
  ex.text = os.str();
  ex.json = "{}";
  return ex;
}
// ---------------------------------------------------------------------------
// Public KernelCache facade
// ---------------------------------------------------------------------------
KernelCache::KernelCache(KernelCacheConfig config) : impl_(std::make_unique<Impl>()) {
  impl_->cfg = std::move(config);
  if (impl_->cfg.persistence_enabled && !impl_->cfg.persistence_root.empty()) {
    impl_->persistence = std::make_unique<FilePersistenceStore>();
    auto r = impl_->persistence->open(impl_->cfg.persistence_root);
    if (r) { impl_->persistence_open = true; }
    else {
      impl_->persistence_open = false;
      impl_->persistence.reset();
      impl_->record_event("persistence", "open failed: " + r.error().message());
    }
  }
  impl_->record_event("cache", "created");
}
KernelCache::~KernelCache() {
  if (impl_) { impl_->record_event("cache", "destroyed"); }
}

const KernelCacheConfig& KernelCache::config() const noexcept { return impl_->cfg; }
const KernelCompatibilityPolicy& KernelCache::compatibility_policy() const noexcept { return impl_->cfg.compatibility; }
const ResidencyPolicy& KernelCache::residency_policy() const noexcept { return impl_->cfg.residency; }

Result<void> KernelCache::register_backend(KernelBackend backend) {
  if (backend.builder == nullptr) return Result<void>(ErrorCode::InvalidArgument, "backend has no builder");
  // Capture the name before the move, because there is no sequencing guarantee
  // between the key expression and the moved-from argument.
  std::string name = backend.name;
  impl_->backends[name] = std::make_shared<KernelBackend>(std::move(backend));
  return Result<void>();
}
Result<void> KernelCache::use_builtin_backends() {
  auto cpu = make_cpu_backend();
  if (cpu) { auto r = register_backend(*cpu); if (!r) return r; }
  if (cuda_backend_available()) {
    auto cuda = make_cuda_backend();
    if (cuda) { auto r = register_backend(*cuda); if (!r) return r; }
  }
  return Result<void>();
}

Result<KernelLease> KernelCache::acquire(const KernelLookupRequest& request) {
  return impl_->acquire_impl(request);
}
Result<KernelLookupResult> KernelCache::lookup(const KernelLookupRequest& request) {
  return impl_->lookup_impl(request);
}
Result<ArtifactDescriptor> KernelCache::build(const KernelLookupRequest& request) {
  return impl_->build_impl(request, nullptr);
}
Result<ArtifactId> KernelCache::put(ArtifactDescriptor desc, std::vector<std::uint8_t> bytes) {
  if (desc.id == ArtifactId{}) desc.id = derive_artifact_id(bytes, desc.generation);
  if (desc.generation.value == 0) desc.generation = ArtifactGeneration(1);
  auto rec = impl_->make_record(desc, std::move(bytes));
  {
    std::lock_guard<std::shared_mutex> lk(impl_->mtx_);
    if (impl_->by_id.count(rec->id)) return Result<ArtifactId>(ErrorCode::AlreadyExists, "artifact id already present");
    impl_->insert_index_locked(rec);
  }
  return rec->id;
}

Result<ValidationDescriptor> KernelCache::validate(const KernelLookupRequest& request, const ArtifactId& artifact) {
  std::shared_ptr<ArtifactRecord> rec;
  {
    std::lock_guard<std::shared_mutex> lk(impl_->mtx_);
    auto it = impl_->by_id.find(artifact);
    if (it == impl_->by_id.end()) return Result<ValidationDescriptor>(ErrorCode::NotFound, "artifact not found");
    rec = it->second;
  }
  auto ld = impl_->find_backend(rec->key);
  if (!ld || !ld->validator) return Result<ValidationDescriptor>(ErrorCode::NotSupported, "no validator for backend");
  // Validate outside the master lock.
  auto res = ld->validator->validate(rec->key, rec->bytes, request.device);
  if (res) {
    std::lock_guard<std::shared_mutex> lk(impl_->mtx_);
    rec->validated = res.value().all_passed();
    rec->desc.validation = res.value();
    if (rec->validated && can_transition(rec->state, ArtifactState::Valid)) rec->state = ArtifactState::Valid;
    else if (!rec->validated && can_transition(rec->state, ArtifactState::Corrupt)) { rec->state = ArtifactState::Corrupt; rec->corrupt_ = true; }
  } else {
    std::lock_guard<std::shared_mutex> lk(impl_->mtx_);
    rec->corrupt_ = true;
    if (can_transition(rec->state, ArtifactState::Corrupt)) rec->state = ArtifactState::Corrupt;
  }
  return res;
}

Result<ArtifactHandle> KernelCache::load(const ArtifactId& artifact) {
  std::shared_ptr<ArtifactRecord> rec;
  {
    std::lock_guard<std::shared_mutex> lk(impl_->mtx_);
    auto it = impl_->by_id.find(artifact);
    if (it == impl_->by_id.end()) return Result<ArtifactHandle>(ErrorCode::NotFound, "artifact not found");
    rec = it->second;
  }
  DeviceDescriptor dev;
  auto pr = impl_->promote_to_tier(rec, ResidencyTier::DeviceResident, dev);
  if (!pr) return Result<ArtifactHandle>(pr.error().code(), pr.error().message());
  std::lock_guard<std::shared_mutex> lk(impl_->mtx_);
  ArtifactHandle h;
  h.id = rec->id; h.generation = rec->generation; h.load_generation = rec->load_generation;
  h.residency_generation = rec->residency_generation;
  h.format = rec->desc.format; h.backend = rec->backend;
  h.native_handle = rec->native_handle; h.namespace_ = rec->tenant;
  h.device_resident = (rec->tier == ResidencyTier::DeviceResident);
  return h;
}

Result<void> KernelCache::unload(const ArtifactId& artifact) {
  std::shared_ptr<ArtifactRecord> rec;
  {
    std::lock_guard<std::shared_mutex> lk(impl_->mtx_);
    auto it = impl_->by_id.find(artifact);
    if (it == impl_->by_id.end()) return Result<void>(ErrorCode::NotFound, "artifact not found");
    rec = it->second;
  }
  auto ld = impl_->find_backend(rec->key);
  if (ld && ld->loader && rec->native_handle) (void)ld->loader->unload(rec->native_handle);
  std::lock_guard<std::shared_mutex> lk(impl_->mtx_);
  rec->native_handle.reset();
  rec->device_footprint = 0;
  if (can_transition(rec->state, ArtifactState::EvictedDevice)) rec->state = ArtifactState::EvictedDevice;
  rec->tier = rec->persistent ? ResidencyTier::PersistentStorage : ResidencyTier::MetadataOnly;
  return Result<void>();
}

Result<void> KernelCache::execute(const ArtifactHandle& handle, void* args, std::size_t arg_bytes) {
  std::shared_ptr<KernelBackend> be;
  for (auto& [name, b] : impl_->backends) if (name == handle.backend) be = b;
  if (!be) return Result<void>(ErrorCode::NotSupported, "no backend registered for handle backend=" + handle.backend);
  if (!be->loader) return Result<void>(ErrorCode::NotSupported, "backend has no loader");
  return be->loader->execute(handle.native_handle, args, arg_bytes);
}

Result<std::vector<InvalidationOutcome>> KernelCache::invalidate(const InvalidationRequest& request) {
  return impl_->invalidate_impl(request);
}
Result<void> KernelCache::evict(const ArtifactId& artifact, bool force) { return impl_->evict_impl(artifact, force); }
Result<void> KernelCache::pin(const ArtifactId& artifact) { return impl_->pin_impl(artifact); }
Result<void> KernelCache::unpin(const ArtifactId& artifact) { return impl_->unpin_impl(artifact); }
Result<std::vector<EvictionCandidate>> KernelCache::eviction_candidates(std::uint32_t top_n) {
  return impl_->eviction_candidates_impl(top_n);
}
Result<std::vector<ArtifactDescriptor>> KernelCache::list(std::optional<std::string> ns,
                                                          std::optional<ArtifactState> state) const {
  return impl_->list_impl(ns, state);
}
Result<ArtifactDescriptor> KernelCache::inspect(const ArtifactId& artifact) const { return impl_->inspect_impl(artifact); }
Stats KernelCache::stats() const {
  std::lock_guard<std::mutex> lk(impl_->stats_mtx_);
  return impl_->stats_;
}
Snapshot KernelCache::snapshot() const { return impl_->snapshot_impl(); }
EventHistory& KernelCache::events() { return impl_->events_; }
Explain KernelCache::explain(const ArtifactId& artifact) const { return impl_->explain_impl(artifact); }
Explain KernelCache::explain(const KernelLookupRequest& request) const {
  // Explain a lookup: derive the compatible result and describe why.
  Explain ex; ex.ok = true;
  auto r = impl_->lookup_impl(request);
  ExplainNode n; n.topic = "lookup";
  if (r) { n.summary = "outcome=" + std::string(lookup_outcome_name(r.value().outcome)); }
  else { n.summary = "error=" + r.error().message(); ex.ok = false; }
  n.details.push_back("op=" + request.key.operation);
  n.details.push_back("digest=" + request.key.digest_hex());
  ex.nodes.push_back(std::move(n));
  ex.text = n.summary;
  return ex;
}
void KernelCache::reset_stats() {
  std::lock_guard<std::mutex> lk(impl_->stats_mtx_);
  impl_->stats_ = Stats{};
}
Result<std::size_t> KernelCache::recover(std::vector<std::string>* rejected, std::vector<std::string>* orphans) {
  return impl_->recover_impl(rejected, orphans);
}
CacheGeneration KernelCache::current_cache_generation() const noexcept { return impl_->cache_generation; }
Result<CacheGeneration> KernelCache::roll_cache_generation() {
  std::lock_guard<std::shared_mutex> lk(impl_->mtx_);
  ++impl_->cache_generation.value;
  // A new cache generation does not invalidate artifacts, but generation
  // authority for new operations moves forward.
  return impl_->cache_generation;
}

}  // namespace kernelcache