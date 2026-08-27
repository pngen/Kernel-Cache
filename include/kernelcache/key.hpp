// key.hpp - canonical typed KernelCompatibilityKey.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <functional>

#include "kernelcache/descriptors.hpp"
#include "kernelcache/sha256.hpp"
#include "kernelcache/result.hpp"

namespace kernelcache {

// ---------------------------------------------------------------------------
// KernelCompatibilityKey
// ---------------------------------------------------------------------------
// This is the canonical, typed identity used to make safe-reuse decisions. It is
// NOT a hash of a filename or operator name. Every correctness-affecting input
// must be encoded; metadata that provably cannot affect execution correctness is
// intentionally excluded. The key is finalized into a canonical byte encoding
// which is hashed with SHA-256 to produce a deterministic digest. The full typed
// metadata is preserved for explainability.
// ---------------------------------------------------------------------------
class KernelCompatibilityKey {
 public:
  KernelCompatibilityKey();

  // --- logical operation identity -------------------------------------------
  std::string operation;
  std::string artifact_format;

  // --- accelerator / architecture -------------------------------------------
  DeviceVendor vendor = DeviceVendor::Unknown;
  AcceleratorFamily family = AcceleratorFamily::None;
  std::string arch;                       // e.g. "sm_120", "x86_64"
  ComputeCapability capability;
  DeviceId device_id;                     // exact device when specialization needs it

  // --- runtime backend ------------------------------------------------------
  std::string runtime_backend;
  std::uint32_t runtime_version_major = 0;
  std::uint32_t runtime_version_minor = 0;

  // --- compiler -------------------------------------------------------------
  std::string compiler_name;
  std::uint32_t compiler_version_major = 0;
  std::uint32_t compiler_version_minor = 0;
  std::uint32_t compiler_version_patch = 0;
  std::string compiler_backend;

  // --- ABI / interface ------------------------------------------------------
  std::string abi_name;
  std::uint64_t abi_version = 1;
  std::string kernel_interface_signature;    // canonical parameter list
  std::vector<std::string> scalar_param_types;

  // --- tensor contract ------------------------------------------------------
  std::vector<Datatype> dtypes;
  std::vector<TensorLayout> layouts;
  std::uint32_t rank = 0;

  // --- shape specialization -------------------------------------------------
  std::vector<std::int64_t> shape;              // exact dims; empty => unconstrained
  std::vector<std::string> symbolic_shape_constraints;

  // --- memory contract ------------------------------------------------------
  std::uint32_t alignment_bytes = 0;
  std::uint32_t shared_mem_bytes = 0;

  // --- launch ABI -----------------------------------------------------------
  std::string launch_abi;
  std::uint32_t block_x = 0, block_y = 0, block_z = 0;

  // --- codegen / specialization ---------------------------------------------
  std::vector<std::string> codegen_flags;                 // sorted canonical order
  std::vector<std::pair<std::string, std::string>> specialization;  // sorted

  // --- model / operator revision where semantically required ----------------
  std::string model_revision;
  std::string operator_revision;

  // --- quantization ---------------------------------------------------------
  std::string quantization_kind;
  std::uint32_t quantization_bits = 0;
  bool quantization_symmetric = true;

  // ---------------------------------------------------------------------------
  // Finalized state. Never mutated after finalize().
  // ---------------------------------------------------------------------------
  // Recompute digest + canonical text. Returns false on malformed input such as
  // a negative rank or inconsistent dtypes/layouts/rank counts.
  bool finalize() noexcept;

  // Reconstruct a typed key from the canonical binary encoding produced by
  // finalize(). Used by the persistence/recovery layer to bridge processes.
  static Result<KernelCompatibilityKey> from_canonical(std::span<const std::uint8_t> bytes) noexcept;

  // Canonical binary encoding (already prefixed with SHA-256 digest at build()).
  const std::vector<std::uint8_t>& canonical_bytes() const noexcept { return canonical_; }
  const Sha256Digest& digest() const noexcept { return digest_; }
  const std::string& digest_hex() const noexcept { return digest_hex_; }
  const std::string& canonical_text() const noexcept { return canonical_text_; }

  bool finalized() const noexcept { return finalized_; }

  // Validation of key structural invariants and addressable-member coverage.
  Result<void> validate() const noexcept;

  bool operator==(const KernelCompatibilityKey& o) const noexcept;
  std::size_t hash() const noexcept;

 private:
  std::vector<std::uint8_t> canonical_;
  Sha256Digest digest_{};
  std::string digest_hex_;
  std::string canonical_text_;
  bool finalized_ = false;
};

struct KernelCompatibilityKeyHash {
  std::size_t operator()(const KernelCompatibilityKey& k) const noexcept { return k.hash(); }
};

// Human-readable summary including the digest.
std::string key_summary(const KernelCompatibilityKey& k);

}  // namespace kernelcache
namespace std {
template <>
struct hash<kernelcache::KernelCompatibilityKey> {
  std::size_t operator()(const kernelcache::KernelCompatibilityKey& k) const noexcept { return k.hash(); }
};
}  // namespace std
