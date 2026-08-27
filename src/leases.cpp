#include "kernelcache/lookup.hpp"
#include "src/impl.hpp"
#include "src/internal.hpp"
#include "kernelcache/lifecycle.hpp"

#include <atomic>
#include <chrono>

namespace kernelcache {

using internal::ArtifactRecord;

struct KernelLease::State {
  KernelCache::Impl* impl = nullptr;
  std::shared_ptr<ArtifactRecord> rec;
  std::uint64_t gen_value = 0;
  std::uint64_t load_gen_value = 0;
  std::atomic<bool> released{false};
};

struct KernelReservation::State {
  KernelCache::Impl* impl = nullptr;
  KernelCompatibilityKey key;
  std::atomic<bool> released{false};
};

namespace {
std::uint64_t now_ns() {
  auto t = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t).count());
}
}  // namespace

// ---------------------------------------------------------------------------
// grant_lease
// ---------------------------------------------------------------------------
Result<KernelLease> KernelCache::Impl::grant_lease(const std::shared_ptr<ArtifactRecord>& rec,
                                                   KernelLookupRequest& request) {
  auto st = std::make_shared<KernelLease::State>();
  st->impl = this;
  st->rec = rec;
  {
    std::lock_guard<std::shared_mutex> lk(mtx_);
    if (rec->invalidated_ || rec->corrupt_ || !kernelcache::is_eligible(rec->state)) {
      return Result<KernelLease>(ErrorCode::StaleArtifact, "artifact no longer eligible at lease time");
    }
    ++rec->lease_count;
    ++rec->access_count;
    rec->last_access_ns = now_ns();
    ++rec->reuse_count;
    if (can_transition(rec->state, ArtifactState::InUse) &&
        (rec->state == ArtifactState::Valid || rec->state == ArtifactState::Persisted ||
         rec->state == ArtifactState::ResidentDevice || rec->state == ArtifactState::ResidentHost)) {
      rec->state = ArtifactState::InUse;
    }
    if (request.pin) ++rec->pin_count;
    st->gen_value = rec->generation.value;
    st->load_gen_value = rec->load_generation.value;
    bump([&](Stats& s){ ++s.active_leases; });
  }
  KernelLease lease;
  lease.state_ = std::move(st);
  return lease;
}

// ---------------------------------------------------------------------------
// KernelLease
// ---------------------------------------------------------------------------
KernelLease::KernelLease(KernelLease&& other) noexcept : state_(std::move(other.state_)) {}
KernelLease& KernelLease::operator=(KernelLease&& other) noexcept {
  if (this != &other) { release(); state_ = std::move(other.state_); }
  return *this;
}
KernelLease::~KernelLease() { release(); }

bool KernelLease::valid() const noexcept { return state_ != nullptr && !state_->released.load(); }
ArtifactId KernelLease::artifact_id() const { return state_ ? state_->rec->id : ArtifactId{}; }
ArtifactGeneration KernelLease::generation() const { return state_ ? ArtifactGeneration(state_->gen_value) : ArtifactGeneration{}; }
ResidencyTier KernelLease::residency() const { return state_ ? state_->rec->tier : ResidencyTier::None; }
LoadedModuleHandle KernelLease::native_handle() const { return state_ ? state_->rec->native_handle : nullptr; }
const KernelCompatibilityKey& KernelLease::key() const { return state_->rec->key; }

void KernelLease::release() {
  if (!state_) return;
  if (state_->released.exchange(true)) return;
  KernelCache::Impl* impl = state_->impl;
  auto rec = state_->rec;
  auto lgv = state_->load_gen_value;
  if (impl && rec) impl->on_lease_release(rec, lgv);
  state_.reset();
}

Result<void> KernelLease::touch() {
  if (!valid()) return Result<void>(ErrorCode::StaleArtifact, "lease is not valid");
  std::lock_guard<std::shared_mutex> lk(state_->impl->mtx_);
  auto& rec = *state_->rec;
  rec.last_access_ns = now_ns();
  ++rec.reuse_count;
  return Result<void>();
}

// ---------------------------------------------------------------------------
// KernelReservation
// ---------------------------------------------------------------------------
KernelReservation::KernelReservation(KernelReservation&& other) noexcept : state_(std::move(other.state_)) {}
KernelReservation& KernelReservation::operator=(KernelReservation&& other) noexcept {
  if (this != &other) { release(); state_ = std::move(other.state_); }
  return *this;
}
KernelReservation::~KernelReservation() { release(); }
bool KernelReservation::valid() const noexcept { return state_ != nullptr && !state_->released.load(); }
void KernelReservation::release() {
  if (!state_) return;
  if (state_->released.exchange(true)) return;
  state_.reset();
}

}  // namespace kernelcache
