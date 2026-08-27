// artifact.hpp - immutable artifact descriptor and execution handle.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include "kernelcache/identifiers.hpp"
#include "kernelcache/descriptors.hpp"
#include "kernelcache/key.hpp"
#include "kernelcache/backend.hpp"

namespace kernelcache {

// Immutable descriptor of a published artifact. Never mutated; a rebuilt
// artifact gets a new generation and identity.
struct ArtifactDescriptor {
  ArtifactId id;
  ArtifactGeneration generation;
  KernelCompatibilityKey key;      // finalized
  std::string format;              // "cubin", "kc-synth", ...
  std::string namespace_;
  std::string backend;
  ArtifactSizes sizes;
  CompilationDescriptor compilation;
  ValidationDescriptor validation;
  ProvenanceDescriptor provenance;
  std::string bytes_sha256;        // checksum of the artifact payload
  std::uint64_t published_at_ns = 0;
  std::uint32_t kernel_count = 1;
};

// An execution-ready shallow handle retrieved through the cache. It carries the
// artifact identity together with an opaque per-backend native (module) handle
// when the artifact is device- or host-resident, plus a load generation.
struct ArtifactHandle {
  ArtifactId id;
  ArtifactGeneration generation;
  LoadGeneration load_generation;
  ResidencyGeneration residency_generation;
  std::string format;
  std::string backend;
  LoadedModuleHandle native_handle;   // device/host module or null
  std::string namespace_;
  bool device_resident = false;
};

}  // namespace kernelcache