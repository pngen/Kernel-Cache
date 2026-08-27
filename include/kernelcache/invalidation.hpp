// invalidation.hpp - structured invalidation requests.
#pragma once

#include <string>

#include "kernelcache/identifiers.hpp"
#include "kernelcache/key.hpp"
#include "kernelcache/result.hpp"

namespace kernelcache {

enum class InvalidationTarget : std::uint8_t {
  ArtifactId = 0,
  KernelId = 1,
  CompatibilityKey = 2,
  Operation = 3,
  CompilerGeneration = 4,
  RuntimeGeneration = 5,
  Architecture = 6,
  ModelRevision = 7,
  OperatorRevision = 8,
  Namespace = 9,
  All = 10,
};

const char* invalidation_target_name(InvalidationTarget t) noexcept;

struct InvalidationRequest {
  InvalidationTarget target = InvalidationTarget::ArtifactId;
  std::optional<std::uint64_t> artifact_lo;   // lo64 of ArtifactId when target==ArtifactId
  std::optional<std::uint64_t> artifact_hi;
  std::optional<std::uint64_t> kernel_lo;     // lo64 of KernelId
  std::optional<std::uint64_t> kernel_hi;
  std::optional<KernelCompatibilityKey> key;  // when target==CompatibilityKey
  std::string operation;
  std::string compiler_generation;
  std::string runtime_generation;
  std::string architecture;
  std::string model_revision;
  std::string operator_revision;
  std::string namespace_;
  std::string reason;
  bool hard = false;  // if true, active leases are forcibly retired (rare)
};

struct InvalidationOutcome {
  InvalidationTarget target = InvalidationTarget::ArtifactId;
  bool applied = false;
  std::uint32_t affected = 0;
  std::vector<ArtifactId> affected_ids;
  std::string message;
  bool hard = false;
};

}  // namespace kernelcache
