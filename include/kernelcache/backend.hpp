// backend.hpp - vendor-neutral build/validate/load backend interfaces.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include "kernelcache/key.hpp"
#include "kernelcache/result.hpp"
#include "kernelcache/descriptors.hpp"

namespace kernelcache {

// ---------------------------------------------------------------------------
// BuildRequest / BuildResult
// ---------------------------------------------------------------------------
struct BuildRequest {
  KernelCompatibilityKey key;    // finalized
  std::string source;            // kernel source text
  std::string backend;           // "cpu-synth", "cuda"
  DeviceDescriptor device;
  CompilationDescriptor compile;
  std::string namespace_;
  std::string logical_operation;
};

// An opaque per-backend loaded-module handle returned by load().
using LoadedModuleHandle = std::shared_ptr<void>;

struct BuildResult {
  ArtifactId id;
  ArtifactGeneration generation;
  std::string artifact_format;  // "cubin", "kc-synth", "ptx"
  std::vector<std::uint8_t> bytes;
  ArtifactSizes sizes;
  ProvenanceDescriptor provenance;
  CompilationDescriptor compilation;
  double build_seconds = 0.0;
  std::uint32_t kernel_count = 0;
};

// ---------------------------------------------------------------------------
// KernelBuilder / KernelCompiler interface
// ---------------------------------------------------------------------------
class KernelBuilder {
 public:
  virtual ~KernelBuilder() = default;
  virtual std::string backend_name() const = 0;
  // Whether this builder can produce an artifact for the given key.
  virtual bool can_build(const KernelCompatibilityKey& key) const = 0;
  // Perform the actual compile/build. Must be safe to call from a coordination
  // thread (no cache locks held). Real compilation; not a copy of prebuilt bytes.
  virtual Result<BuildResult> build(const BuildRequest& req) = 0;
};

// ---------------------------------------------------------------------------
// KernelValidator interface
// ---------------------------------------------------------------------------
class KernelValidator {
 public:
  virtual ~KernelValidator() = default;
  virtual std::string backend_name() const = 0;
  // Validate artifact integrity/format/ABI and (where the backend supports it)
  // load, launch, and compare against a deterministic reference.
  virtual Result<ValidationDescriptor> validate(
      const KernelCompatibilityKey& key,
      const std::vector<std::uint8_t>& bytes,
      const DeviceDescriptor& device) = 0;
};

// ---------------------------------------------------------------------------
// KernelLoader interface
// ---------------------------------------------------------------------------
class KernelLoader {
 public:
  virtual ~KernelLoader() = default;
  virtual std::string backend_name() const = 0;
  // Load an artifact into the device (or host) so it may be executed.
  virtual Result<LoadedModuleHandle> load(
      const KernelCompatibilityKey& key,
      const std::vector<std::uint8_t>& bytes,
      const DeviceDescriptor& device) = 0;
  virtual Result<void> unload(LoadedModuleHandle handle) = 0;
  // Execute a loaded module with the given launch/argument blob. Optional;
  // backends without an execution path return NotSupported.
  virtual Result<void> execute(LoadedModuleHandle handle, void* args, std::size_t arg_bytes) {
    (void)handle; (void)args; (void)arg_bytes;
    return Result<void>(ErrorCode::NotSupported, "backend has no execute path");
  }
  // Best-effort device-resident footprint estimate. true=measured, false=estimated.
  virtual std::pair<std::uint64_t, bool> resident_footprint(
      const LoadedModuleHandle& handle) const = 0;
};

// ---------------------------------------------------------------------------
// Backend bundle
// ---------------------------------------------------------------------------
struct KernelBackend {
  std::string name;
  KernelBuilder* builder = nullptr;
  KernelValidator* validator = nullptr;
  KernelLoader* loader = nullptr;
};

// Built-in backend factories (declared here so the cache can register them).
std::shared_ptr<KernelBackend> make_cpu_backend();
std::shared_ptr<KernelBackend> make_cuda_backend();
bool cuda_backend_available();
// Name of built-in backends available to the cache (for introspection/reports).
std::string builtin_backend_names();

}  // namespace kernelcache