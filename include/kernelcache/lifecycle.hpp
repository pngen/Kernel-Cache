// lifecycle.hpp - explicit artifact lifecycle state machine.
#pragma once

#include "kernelcache/descriptors.hpp"

namespace kernelcache {

bool can_transition(ArtifactState from, ArtifactState to) noexcept;
bool is_terminal(ArtifactState s) noexcept;
bool is_eligible(ArtifactState s) noexcept;
bool is_poisoned(ArtifactState s) noexcept;
const char* transition_reason(ArtifactState from, ArtifactState to) noexcept;
bool can_start(ArtifactState s) noexcept;

}  // namespace kernelcache
