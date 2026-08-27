// descriptors.hpp - typed descriptor and enumeration types.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <compare>
#include <initializer_list>

#include "kernelcache/identifiers.hpp"

namespace kernelcache {

// ---------------------------------------------------------------------------
// Scalar/enumeration model
// ---------------------------------------------------------------------------
enum class Datatype : std::uint8_t {
  None = 0,
  Bool = 1,
  I8 = 2, U8 = 3,
  I16 = 4, U16 = 5,
  I32 = 6, U32 = 7,
  I64 = 8, U64 = 9,
  F16 = 10, BF16 = 11,
  F32 = 12, F64 = 13,
  TF32 = 14,
  Int4 = 15, UInt4 = 16,
};

// Friendly name used in reports and explain output.
const char* datatype_name(Datatype dt) noexcept;
std::uint32_t datatype_size_bytes(Datatype dt) noexcept;

enum class TensorLayout : std::uint8_t {
  None = 0,
  RowMajor = 1,
  ColMajor = 2,
  NCHW = 3,
  NHWC = 4,
  NCWH = 5,
  Blocked = 6,
  Strided = 7,
  Sparse = 8,
};

const char* layout_name(TensorLayout layout) noexcept;

enum class DeviceVendor : std::uint8_t {
  Unknown = 0,
  NVIDIA = 1,
  AMD = 2,
  Intel = 3,
  CPU = 4,
};

const char* vendor_name(DeviceVendor v) noexcept;

enum class AcceleratorFamily : std::uint8_t {
  None = 0,
  CUDA = 1,
  ROCm = 2,
  OneAPI = 3,
  CPU = 4,
};

// Runtime / backend identity.
struct RuntimeBackend {
  std::string name;      // e.g. "cuda", "cpu-synth"
  std::uint32_t version_major = 0;
  std::uint32_t version_minor = 0;
  bool operator==(const RuntimeBackend&) const = default;
};

// Processor ISA target: vendor + family + arch + compute capability.
struct ComputeCapability {
  std::uint8_t major = 0;
  std::uint8_t minor = 0;
  bool operator==(const ComputeCapability&) const noexcept = default;
  auto operator<=>(const ComputeCapability&) const noexcept = default;
};

struct DeviceArchitecture {
  DeviceVendor vendor = DeviceVendor::Unknown;
  AcceleratorFamily family = AcceleratorFamily::None;
  std::string arch_name;             // "sm_120", "x86_64", "AVX512"
  ComputeCapability compute_capability;  // 0.0 means absent
  bool operator==(const DeviceArchitecture&) const = default;
};

// Compiler identity + codegen revision.
struct CompilerIdentity {
  std::string name;       // "nvcc", "msvc", "gcc", "clang", "nvrtc"
  std::uint32_t version_major = 0;
  std::uint32_t version_minor = 0;
  std::uint32_t version_patch = 0;
  std::string backend;    // codegen backend tag
  bool operator==(const CompilerIdentity&) const = default;
};

// ---------------------------------------------------------------------------
// Shape
// ---------------------------------------------------------------------------
// A 4096 value denotes an unspecified symbolic dimension.
inline constexpr std::int64_t kSymbolicDim = -1;

struct ShapeDescriptor {
  // Dimensions; value < 0 denotes an unbound symbolic dimension, otherwise the
  // exact extent. Rank is dims.size().
  std::vector<std::int64_t> dims;

  ShapeDescriptor() = default;
  ShapeDescriptor(std::initializer_list<std::int64_t> d) : dims(d) {}

  std::size_t rank() const noexcept { return dims.size(); }
  bool is_static() const noexcept {
    for (auto d : dims) if (d < 0) return false;
    return true;
  }
  // Number of elements, ignoring symbolic dims (returns 0 if any symbolic).
  std::uint64_t static_size() const noexcept {
    if (!is_static()) return 0;
    std::uint64_t n = 1;
    for (auto d : dims) n *= static_cast<std::uint64_t>(d);
    return n;
  }
  bool operator==(const ShapeDescriptor&) const = default;
};

// ---------------------------------------------------------------------------
// Launch
// ---------------------------------------------------------------------------
struct LaunchDescriptor {
  std::uint32_t grid_x = 1, grid_y = 1, grid_z = 1;
  std::uint32_t block_x = 0, block_y = 1, block_z = 1;  // block_x 0 = auto/unspecified
  std::uint32_t shared_mem_bytes = 0;
  std::uint32_t stream_id = 0;
  bool operator==(const LaunchDescriptor&) const = default;
};

// ---------------------------------------------------------------------------
// Device / Runtime
// ---------------------------------------------------------------------------
struct DeviceDescriptor {
  DeviceId id;
  std::string name;
  DeviceVendor vendor = DeviceVendor::Unknown;
  AcceleratorFamily family = AcceleratorFamily::None;
  std::string arch_name;
  ComputeCapability capability;
  std::uint64_t memory_bytes = 0;   // total, if known
  bool operator==(const DeviceDescriptor&) const = default;
};

// ABI identity for the kernel executable boundary.
struct KernelABI {
  std::string name;            // e.g. "cuda-1.0", "kc-synth-1.0"
  std::uint64_t version = 1;
  bool operator==(const KernelABI&) const = default;
};

// Quantization configuration.
struct QuantizationMode {
  std::string kind;      // "none", "int8-symmetric", "fp16", "int4-awq", ...
  std::uint32_t bits = 0;
  bool symmetric = true;
  bool operator==(const QuantizationMode&) const = default;
};

// ---------------------------------------------------------------------------
// Build / compilation options relevant to code generation semantics.
// ---------------------------------------------------------------------------
struct CodegenFlags {
  std::vector<std::string> flags;  // sorted canonical order
  bool operator==(const CodegenFlags&) const = default;
};

struct SpecializationParams {
  std::vector<std::pair<std::string, std::string>> params;  // key -> value
  bool operator==(const SpecializationParams&) const = default;
};

// ---------------------------------------------------------------------------
// Residency / persistence tiers
// ---------------------------------------------------------------------------
enum class ResidencyTier : std::uint8_t {
  None = 0,
  MetadataOnly = 1,
  PersistentStorage = 2,
  HostResident = 3,
  DeviceResident = 4,
};

const char* residency_tier_name(ResidencyTier t) noexcept;

// ---------------------------------------------------------------------------
// Artifact lifecycle state
// ---------------------------------------------------------------------------
enum class ArtifactState : std::uint8_t {
  Discovered = 0,
  Building,
  Built,
  Validating,
  Valid,
  Loading,
  ResidentHost,
  ResidentDevice,
  Persisted,
  Leasing,
  InUse,
  DemotionPending,
  EvictionPending,
  EvictedDevice,
  EvictedHost,
  Invalidated,
  Corrupt,
  Failed,
  Retired,
  Terminal,
};

const char* artifact_state_name(ArtifactState s) noexcept;

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------
struct ProvenanceDescriptor {
  std::string producer;        // backend name
  std::string build_command;   // canonical, non-secret building command
  std::string source_namespace;
  std::string source_id;       // source identity hash where applicable
  std::string source_text_hash;
  std::string tenant;          // namespace/tenant
  std::uint64_t built_at_ns = 0;
  bool operator==(const ProvenanceDescriptor&) const = default;
};

// ---------------------------------------------------------------------------
// Sizes: host bytes, device bytes, storage bytes.
// ---------------------------------------------------------------------------
struct ArtifactSizes {
  std::uint64_t artifact_bytes = 0;        // full serialized artifact bytes
  std::uint64_t host_bytes = 0;            // host-resident image bytes
  std::uint64_t device_bytes = 0;          // device-resident (loaded) bytes
  std::uint64_t storage_bytes = 0;         // on-disk bytes
  bool device_bytes_estimated = true;      // true if not directly queryable
  bool operator==(const ArtifactSizes&) const = default;
};

// ---------------------------------------------------------------------------
// Descriptors
// ---------------------------------------------------------------------------
struct CompilationDescriptor {
  CompilerIdentity compiler;
  DeviceArchitecture target;
  CodegenFlags codegen_flags;
  std::string compilation_mode;   // "offline-nvcc", "cpu-synth", ...
  bool operator==(const CompilationDescriptor&) const = default;
};

struct ValidationDescriptor {
  bool integrity_hash_checked = false;
  bool format_checked = false;
  bool arch_checked = false;
  bool abi_checked = false;
  bool metadata_consistent = false;
  bool loadable = false;
  bool execution_smoke = false;
  bool reference_compare = false;
  std::string backend_reason;    // human explanation of failure if any
  bool operator==(const ValidationDescriptor&) const = default;
  bool all_passed() const noexcept {
    return integrity_hash_checked && format_checked && arch_checked &&
           abi_checked && metadata_consistent && loadable &&
           execution_smoke && reference_compare;
  }
};

struct ResidencyDescriptor {
  ResidencyTier tier = ResidencyTier::None;
  std::uint64_t last_access_ns = 0;
  std::uint64_t pin_count = 0;
  bool operator==(const ResidencyDescriptor&) const = default;
};

}  // namespace kernelcache
