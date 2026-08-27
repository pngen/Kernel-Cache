#include "kernelcache/lifecycle.hpp"
#include "kernelcache/result.hpp"

namespace kernelcache {

// A tiny, lock-free-by-construction helper that tracks the current state of one
// artifact through validated transitions. The caller holds the cache master lock.
class ArtifactLifecycle {
 public:
  explicit ArtifactLifecycle(ArtifactState initial = ArtifactState::Discovered) : state_(initial) {}

  ArtifactState state() const noexcept { return state_; }
  bool is_terminal() const noexcept { return kernelcache::is_terminal(state_); }
  bool is_poisoned() const noexcept { return kernelcache::is_poisoned(state_); }

  // Attempt a transition. Returns false (and leaves state unchanged) on an
  // illegal transition, otherwise returns the new state.
  Result<ArtifactState> transition(ArtifactState to) noexcept {
    if (!can_transition(state_, to)) {
      return Result<ArtifactState>(ErrorCode::BadStateTransition,
                                   std::string("illegal transition ") + artifact_state_name(state_) +
                                   " -> " + artifact_state_name(to));
    }
    state_ = to;
    return state_;
  }

 private:
  ArtifactState state_;
};

}  // namespace kernelcache
