#include "kernelcache/kernel_cache.hpp"
#include "src/impl.hpp"
#include "src/internal.hpp"
#include "kernelcache/lifecycle.hpp"

#include <chrono>
#include <algorithm>

namespace kernelcache {

using internal::ArtifactRecord;

namespace {
std::uint64_t now_ns() {
  auto t = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t).count());
}
double clamp01(double x) { if (x < 0.0) return 0.0; if (x > 1.0) return 1.0; return x; }
}  // namespace

Result<std::vector<std::uint8_t>> KernelCache::Impl::ensure_bytes(const std::shared_ptr<ArtifactRecord>& rec) {
  {
    std::lock_guard<std::shared_mutex> lk(mtx_);
    if (!rec->bytes.empty()) return rec->bytes;
  }
  if (persistence_open && persistence) {
    auto r = persistence->get(rec->id);
    if (r) {
      std::lock_guard<std::shared_mutex> lk(mtx_);
      rec->bytes = r.value().bytes;
      rec->persistent = true;
      return rec->bytes;
    } else {
      return Result<std::vector<std::uint8_t>>(r.error().code(), r.error().message());
    }
  }
  return Result<std::vector<std::uint8_t>>(ErrorCode::NotFound, "bytes absent and no persistence to reload from");
}

Result<void> KernelCache::Impl::promote_to_tier(const std::shared_ptr<ArtifactRecord>& rec,
                                                ResidencyTier desired,
                                                const DeviceDescriptor& device) {
  if (desired == ResidencyTier::None) return Result<void>();
  ResidencyTier cw;
  { std::lock_guard<std::shared_mutex> lk(mtx_); cw = rec->tier; }
  if (cw >= desired) return Result<void>();

  if (desired == ResidencyTier::DeviceResident) {
    auto bytes = ensure_bytes(rec);
    if (!bytes) return Result<void>(bytes.error().code(), bytes.error().message());
    auto ld = find_backend(rec->key);
    if (!ld || !ld->loader) return Result<void>(ErrorCode::NotSupported, "no loader for host->device promotion");
    auto h = ld->loader->load(rec->key, bytes.value(), device);
    if (!h) return Result<void>(h.error().code(), h.error().message());
    auto fp = ld->loader->resident_footprint(h.value());
    bool installed = false;
    bool stale = false;
    {
      std::lock_guard<std::shared_mutex> lk(mtx_);
      // While the module loaded outside the lock the record may have been
      // invalidated, claimed for eviction, or loaded by a concurrent promotion.
      // Never resurrect or overwrite device state; unload the redundant module.
      if (rec->invalidated_ || rec->corrupt_ || !kernelcache::is_eligible(rec->state)) {
        stale = true;
      } else if (rec->tier != ResidencyTier::DeviceResident) {
        rec->native_handle = h.value();
        rec->load_generation = LoadGeneration(rec->load_generation.value + 1);
        rec->residency_generation = ResidencyGeneration(rec->residency_generation.value + 1);
        rec->device_footprint = fp.first;
        rec->device_footprint_estimated = fp.second;
        rec->device_bytes_used = fp.first;
        if (can_transition(rec->state, ArtifactState::ResidentDevice)) rec->state = ArtifactState::ResidentDevice;
        rec->tier = ResidencyTier::DeviceResident;
        bump([&](Stats& s){ ++s.active_device_loads; });
        installed = true;
      }
    }
    if (!installed) {
      (void)ld->loader->unload(h.value());
      if (stale) return Result<void>(ErrorCode::StaleArtifact, "artifact became ineligible while loading");
      return Result<void>();
    }
    record_event("promote", "id=" + rec->id.str() + " to device");
    evict_to_fit(ResidencyTier::DeviceResident);
    return Result<void>();
  }

  if (desired == ResidencyTier::HostResident) {
    auto bytes = ensure_bytes(rec);
    if (!bytes) return Result<void>(bytes.error().code(), bytes.error().message());
    std::lock_guard<std::shared_mutex> lk(mtx_);
    if (rec->invalidated_ || rec->corrupt_ || !kernelcache::is_eligible(rec->state))
      return Result<void>(ErrorCode::StaleArtifact, "artifact became ineligible while loading");
    // A concurrent promotion may already have made it host- or device-resident;
    // never demote it. Restore host bytes an eviction freed meanwhile.
    if (rec->tier >= ResidencyTier::HostResident) return Result<void>();
    if (rec->bytes.empty()) rec->bytes = bytes.value();
    rec->tier = ResidencyTier::HostResident;
    if (can_transition(rec->state, ArtifactState::ResidentHost)) rec->state = ArtifactState::ResidentHost;
    rec->residency_generation = ResidencyGeneration(rec->residency_generation.value + 1);
    return Result<void>();
  }
  return Result<void>();
}

EvictionScore compute_eviction_score(const ArtifactRecord& rec, std::uint64_t now, const ResidencyPolicy& pol) {
  EvictionScore s;
  std::uint64_t age = now > rec.last_access_ns ? now - rec.last_access_ns : 0;
  double last_access_sec = static_cast<double>(age) / 1.0e9;
  s.recency = clamp01(last_access_sec / 3600.0) * 1.0;
  s.frequency = clamp01(static_cast<double>(rec.access_count) / 100.0) * -1.0;
  s.compile_cost = -static_cast<double>(rec.compile_cost_ns) / 1.0e9;
  s.reload_cost = -static_cast<double>(rec.reload_cost_ns) / 1.0e9;
  s.size_cost = static_cast<double>(rec.desc.sizes.host_bytes) / (1024.0 * 1024.0);
  s.device_footprint = static_cast<double>(rec.device_footprint) / (1024.0 * 1024.0);
  s.pin_penalty = static_cast<double>(rec.pin_count) * -100.0;
  s.expected_reuse = static_cast<double>(rec.reuse_count) * -0.01;
  s.namespace_priority = pol.namespace_priority * -1.0;
  s.total = std::max(0.0,
      s.recency + s.frequency +
      (pol.compile_cost_aware ? s.compile_cost : 0.0) +
      (pol.reload_cost_aware ? s.reload_cost : 0.0) +
      (pol.size_aware ? s.size_cost : 0.0) +
      s.pin_penalty + s.expected_reuse + s.namespace_priority);
  return s;
}

void KernelCache::Impl::evict_to_fit(ResidencyTier tier) {
  for (;;) {
    std::uint64_t budget = (tier == ResidencyTier::DeviceResident) ? cfg.residency.device_budget_bytes : cfg.residency.host_budget_bytes;
    bool apply = (tier == ResidencyTier::DeviceResident) ? cfg.residency.apply_device_budget : cfg.residency.apply_host_budget;
    if (!apply) return;
    std::uint64_t used = 0;
    ArtifactId vid; std::shared_ptr<ArtifactRecord> victim; double best_score = -1e300;
    {
      std::lock_guard<std::shared_mutex> lk(mtx_);
      for (auto& [id, rec] : by_id) {
        // Every resident artifact counts against the budget; only unpinned,
        // unleased, healthy, eligible ones may be chosen as victims.
        if (tier == ResidencyTier::DeviceResident) {
          used += rec->device_footprint;
          if (rec->tier != ResidencyTier::DeviceResident) continue;
        } else {
          if (rec->tier != ResidencyTier::HostResident && rec->tier != ResidencyTier::DeviceResident) continue;
          used += rec->desc.sizes.host_bytes;
        }
        if (rec->pin_count > 0 || rec->lease_count > 0) continue;
        if (rec->invalidated_ || rec->corrupt_) continue;
        if (!kernelcache::is_eligible(rec->state)) continue;
        auto s = compute_eviction_score(*rec, now_ns(), cfg.residency);
        if (s.total > best_score) { best_score = s.total; vid = id; victim = rec; }
      }
      if (!victim) return;
      if (used <= budget) return;
    }
    // Stop if the victim could not be evicted (claimed concurrently) rather
    // than selecting it again.
    if (!evict_impl(vid, false)) return;
  }
}

Result<std::vector<EvictionCandidate>> KernelCache::Impl::eviction_candidates_impl(std::uint32_t top_n) {
  std::vector<EvictionCandidate> out;
  std::lock_guard<std::shared_mutex> lk(mtx_);
  for (auto& [id, rec] : by_id) {
    if (rec->pin_count > 0 || rec->lease_count > 0) continue;
    if (rec->invalidated_ || rec->corrupt_) continue;
    EvictionCandidate c;
    c.artifact_lo = id.lo(); c.artifact_hi = id.hi();
    c.score = compute_eviction_score(*rec, now_ns(), cfg.residency);
    c.tier = residency_tier_name(rec->tier);
    c.reason = "recency=" + std::to_string(c.score.recency) + " freq=" + std::to_string(c.score.frequency) +
               " compile=" + std::to_string(c.score.compile_cost) + " reload=" + std::to_string(c.score.reload_cost) +
               " size=" + std::to_string(c.score.size_cost) + " pin=" + std::to_string(c.score.pin_penalty);
    out.push_back(std::move(c));
  }
  std::sort(out.begin(), out.end(), [](const EvictionCandidate& a, const EvictionCandidate& b) {
    if (b.score.total != a.score.total) return a.score.total > b.score.total;
    return std::make_pair(a.artifact_hi, a.artifact_lo) > std::make_pair(b.artifact_hi, b.artifact_lo);
  });
  if (out.size() > top_n) out.resize(static_cast<std::size_t>(top_n));
  return out;
}

}  // namespace kernelcache
