// compatibility.hpp - the compatibility engine.
#pragma once

#include <string>
#include <vector>

#include "kernelcache/key.hpp"
#include "kernelcache/result.hpp"

namespace kernelcache {

// A compatibility decision is a structured, explainable outcome. It never
// silently coerces an incompatible request; an incompatible result is always an
// explicit reason.
enum class CompatibilityReason : std::uint8_t {
  NotEvaluated = 0,
  ExactCompatible = 1,
  CompatibleWithDynamicShapeConstraint = 2,
  CompatibleWithRuntimeValidation = 3,
  IncompatibleArchitecture = 10,
  IncompatibleRuntime = 11,
  IncompatibleCompilerABI = 12,
  IncompatibleKernelABI = 13,
  IncompatibleDatatype = 14,
  IncompatibleLayout = 15,
  IncompatibleShape = 16,
  IncompatibleAlignment = 17,
  IncompatibleSpecialization = 18,
  IncompatibleQuantization = 19,
  InvalidArtifact = 20,
  StaleArtifact = 21,
  CorruptArtifact = 22,
  PolicyRejected = 23,
};

const char* compatibility_reason_name(CompatibilityReason r) noexcept;
std::string to_string(CompatibilityReason r);

struct KernelCompatibilityDecision {
  CompatibilityReason reason = CompatibilityReason::NotEvaluated;
  bool compatible = false;
  std::vector<std::string> notes;

  bool is_compatible() const noexcept { return compatible; }
  bool is_exact() const noexcept { return reason == CompatibilityReason::ExactCompatible; }
  std::string summary() const;
};

// The policy configures how strictly the engine may decide. Kept separate from
// the key so that correctness rules are not hidden in a scalar.
struct CompatibilityPolicy {
  // If true, a shape mismatch is tolerated when the candidate declares symbolic
  // constraints that the request satisfies.
  bool allow_dynamic_shape_constraints = false;
  // If true, a candidate may be returned when the only difference is
  // re-validatable at runtime (e.g. exact device id) and the runtime can verify.
  bool allow_runtime_validation = false;
  // Require the candidate generation to equal the cache authority generation.
  bool require_current_generation = true;
  // Require exact ABI (compiler + kernel) match rather than only a compatible one.
  bool require_exact_abi = false;
  // If true, datatype must match exactly; if false, safe widening may be allowed
  // when the backend declares a precedence.
  bool require_exact_datatype = true;
  // Alignments must match exactly.
  bool require_exact_alignment = true;
};

// Evaluate a request key against a candidate artifact key. The decision is a
// deterministic function of the two keys and the policy.
KernelCompatibilityDecision evaluate_compatibility(const KernelCompatibilityKey& request,
                                                   const KernelCompatibilityKey& candidate,
                                                   const CompatibilityPolicy& policy);

// Alias used by the public cache API.
using KernelCompatibilityPolicy = CompatibilityPolicy;

// Perceived-distance helper used to order candidates from most to least
// restrictive within the same decided class. Lower is "closer".
int compatibility_distance(const KernelCompatibilityDecision& d);

}  // namespace kernelcache