// internal.hpp - internal (non-installed) cache state types.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <future>

#include "kernelcache/kernelcache.hpp"

namespace kernelcache {
namespace internal {

// Deferred declaration of the contract types.
struct ArtifactRecord;

// An in-flight single-flight build. Exactly one owner produces the result;
// waiters observe it.
struct BuildFence {
  struct Outcome {
    ArtifactId id;
    ArtifactGeneration generation;
    std::vector<std::uint8_t> bytes;
    ArtifactDescriptor desc;
    std::string error;
    ErrorCode error_code = ErrorCode::None;
    bool succeeded = false;
  };
  BuildAttemptId attempt;
  std::string backend;
  std::promise<Outcome> promise;
  std::shared_future<Outcome> future;
  std::uint64_t started_ns = 0;
  ArtifactGeneration build_generation;  // the generation this attempt targets
};

// Immutable identity + mutable lifecycle/accounting, all guarded by the cache
// master lock (mtx_ in the Impl). Backend-native handles are stored here.
struct ArtifactRecord {
  ArtifactId id;
  ArtifactGeneration generation;
  KernelCompatibilityKey key;
  ArtifactDescriptor desc;
  std::vector<std::uint8_t> bytes;

  // Mutable, master-lock-guarded.
  ArtifactState state = ArtifactState::Discovered;
  ResidencyTier tier = ResidencyTier::None;
  std::uint64_t last_access_ns = 0;
  std::uint64_t access_count = 0;
  std::uint64_t reuse_count = 0;
  std::uint64_t pin_count = 0;
  std::uint64_t lease_count = 0;
  LoadedModuleHandle native_handle;
  LoadGeneration load_generation;
  ResidencyGeneration residency_generation;
  std::uint64_t device_footprint = 0;
  bool device_footprint_estimated = true;
  bool persistent = false;
  std::string tenant;
  std::string operation;
  std::string arch;
  std::string backend;
  KernelId kernel_id;
  // A per-artifact validation-stamp used to avoid re-validating.
  bool validated = false;
  bool invalidated_ = false;
  bool corrupt_ = false;
  std::uint64_t compile_cost_ns = 0;
  std::uint64_t reload_cost_ns = 0;
  std::uint64_t device_bytes_used = 0;
};

// Structured error wrapper used internally by the coordinator.
struct CoordinationError {
  ErrorCode code = ErrorCode::None;
  std::string message;
};

// A small helper to make a Result from a CoordinationError.
template <typename T>
Result<T> fail_rc(ErrorCode code, std::string msg) {
  return Result<T>(code, std::move(msg));
}

inline Result<void> fail_rc(ErrorCode code, std::string msg) {
  return Result<void>(code, std::move(msg));
}


// A read-only snapshot of candidate state taken under the shared lock.
struct CandidateSnapshot {
  std::shared_ptr<ArtifactRecord> rec;
  KernelCompatibilityKey key;
  ArtifactGeneration gen;
  ArtifactState state;
  ResidencyTier tier;
  bool persistent = false;
  bool validated = false;
  bool invalidated = false;
  bool corrupt = false;
  std::string operation;
  std::string backend;
  KernelCompatibilityDecision decision;
};

}  // namespace internal

// Secondary-index consistency checkers (implemented in artifact_store.cpp /
// index.cpp). They verify that canonical state and indices agree.
bool check_index_consistency(
    const std::unordered_map<ArtifactId, std::shared_ptr<internal::ArtifactRecord>>& by_id,
    const std::unordered_map<KernelCompatibilityKey, std::vector<ArtifactId>>& by_key,
    const std::unordered_map<std::string, std::vector<ArtifactId>>& by_operation,
    std::string* detail);

}  // namespace kernelcache