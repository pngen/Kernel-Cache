#include "kernelcache/compatibility.hpp"

#include <algorithm>
#include <sstream>

namespace kernelcache {

const char* compatibility_reason_name(CompatibilityReason r) noexcept {
  switch (r) {
    case CompatibilityReason::NotEvaluated: return "not-evaluated";
    case CompatibilityReason::ExactCompatible: return "exact-compatible";
    case CompatibilityReason::CompatibleWithDynamicShapeConstraint: return "compatible-with-dynamic-shape-constraint";
    case CompatibilityReason::CompatibleWithRuntimeValidation: return "compatible-with-runtime-validation";
    case CompatibilityReason::IncompatibleArchitecture: return "incompatible-architecture";
    case CompatibilityReason::IncompatibleRuntime: return "incompatible-runtime";
    case CompatibilityReason::IncompatibleCompilerABI: return "incompatible-compiler-abi";
    case CompatibilityReason::IncompatibleKernelABI: return "incompatible-kernel-abi";
    case CompatibilityReason::IncompatibleDatatype: return "incompatible-datatype";
    case CompatibilityReason::IncompatibleLayout: return "incompatible-layout";
    case CompatibilityReason::IncompatibleShape: return "incompatible-shape";
    case CompatibilityReason::IncompatibleAlignment: return "incompatible-alignment";
    case CompatibilityReason::IncompatibleSpecialization: return "incompatible-specialization";
    case CompatibilityReason::IncompatibleQuantization: return "incompatible-quantization";
    case CompatibilityReason::InvalidArtifact: return "invalid-artifact";
    case CompatibilityReason::StaleArtifact: return "stale-artifact";
    case CompatibilityReason::CorruptArtifact: return "corrupt-artifact";
    case CompatibilityReason::PolicyRejected: return "policy-rejected";
  }
  return "unknown";
}

std::string to_string(CompatibilityReason r) { return compatibility_reason_name(r); }

std::string KernelCompatibilityDecision::summary() const {
  std::ostringstream os;
  os << "compatible=" << (compatible ? "yes" : "no")
     << " reason=" << compatibility_reason_name(reason);
  for (auto& n : notes) os << " | " << n;
  return os.str();
}

namespace {
bool same(const std::string& a, const std::string& b) { return a == b; }

bool shapes_match(const std::vector<std::int64_t>& a, const std::vector<std::int64_t>& b) {
  if (a == b) return true;
  if (a.empty() || b.empty()) return true;  // unconstrained matches anything
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i] < 0 || b[i] < 0) continue;  // symbolic on either side
    if (a[i] != b[i]) return false;
  }
  return true;
}

bool dtypes_match(const std::vector<Datatype>& a, const std::vector<Datatype>& b) {
  if (a == b) return true;
  if (a.empty() || b.empty()) return true;
  if (a.size() != b.size()) return false;
  return a == b;
}
}  // namespace

KernelCompatibilityDecision evaluate_compatibility(const KernelCompatibilityKey& req,
                                                   const KernelCompatibilityKey& cand,
                                                   const CompatibilityPolicy& policy) {
  KernelCompatibilityDecision d;
  const KernelCompatibilityKey* r = &req;
  const KernelCompatibilityKey* c = &cand;
  // Ensure both are finalized before comparing digests.
  const_cast<KernelCompatibilityKey*>(r)->finalize();
  const_cast<KernelCompatibilityKey*>(c)->finalize();

  if (r->digest() == c->digest()) {
    d.reason = CompatibilityReason::ExactCompatible;
    d.compatible = true;
    d.notes.push_back("canonical key digest matches exactly");
    return d;
  }

  // Architecture.
  if (c->vendor != DeviceVendor::Unknown && r->vendor != DeviceVendor::Unknown &&
      c->vendor != r->vendor) {
    d.reason = CompatibilityReason::IncompatibleArchitecture;
    d.notes.push_back("device vendor differs: candidate=" + std::string(vendor_name(c->vendor)) +
                      " request=" + std::string(vendor_name(r->vendor)));
    return d;
  }
  if (!c->arch.empty() && !r->arch.empty() && c->arch != r->arch) {
    d.reason = CompatibilityReason::IncompatibleArchitecture;
    d.notes.push_back("arch differs: candidate=" + c->arch + " request=" + r->arch);
    return d;
  }
  if (c->capability != ComputeCapability{} && r->capability != ComputeCapability{} &&
      c->capability != r->capability) {
    d.reason = CompatibilityReason::IncompatibleArchitecture;
    d.notes.push_back("compute capability differs");
    return d;
  }

  // Runtime backend.
  if (!c->runtime_backend.empty() && !r->runtime_backend.empty() &&
      c->runtime_backend != r->runtime_backend) {
    d.reason = CompatibilityReason::IncompatibleRuntime;
    d.notes.push_back("runtime backend differs: candidate=" + c->runtime_backend + " request=" + r->runtime_backend);
    return d;
  }
  if (c->runtime_version_major && r->runtime_version_major &&
      c->runtime_version_major != r->runtime_version_major) {
    d.reason = CompatibilityReason::IncompatibleRuntime;
    d.notes.push_back("runtime major version differs");
    return d;
  }

  // Compiler / ABI.
  if (!c->compiler_name.empty() && !r->compiler_name.empty() &&
      c->compiler_name != r->compiler_name) {
    d.reason = CompatibilityReason::IncompatibleCompilerABI;
    d.notes.push_back("compiler differs: candidate=" + c->compiler_name + " request=" + r->compiler_name);
    return d;
  }
  if (c->compiler_version_major && r->compiler_version_major &&
      c->compiler_version_major != r->compiler_version_major) {
    d.reason = CompatibilityReason::IncompatibleCompilerABI;
    d.notes.push_back("compiler major version differs");
    return d;
  }

  // Kernel ABI.
  if (!c->abi_name.empty() && !r->abi_name.empty() && c->abi_name != r->abi_name) {
    d.reason = CompatibilityReason::IncompatibleKernelABI;
    d.notes.push_back("kernel abi differs: candidate=" + c->abi_name + " request=" + r->abi_name);
    return d;
  }
  if (c->abi_version != r->abi_version) {
    d.reason = CompatibilityReason::IncompatibleKernelABI;
    d.notes.push_back("abi version differs");
    return d;
  }
  if (!c->kernel_interface_signature.empty() && !r->kernel_interface_signature.empty() &&
      c->kernel_interface_signature != r->kernel_interface_signature) {
    d.reason = CompatibilityReason::IncompatibleKernelABI;
    d.notes.push_back("kernel interface signature differs");
    return d;
  }
  if (policy.require_exact_abi &&
      (c->scalar_param_types != r->scalar_param_types)) {
    d.reason = CompatibilityReason::IncompatibleKernelABI;
    d.notes.push_back("scalar parameter schema differs under exact-abi policy");
    return d;
  }

  // Datatype.
  if (policy.require_exact_datatype && !dtypes_match(c->dtypes, r->dtypes)) {
    d.reason = CompatibilityReason::IncompatibleDatatype;
    d.notes.push_back("tensor datatype(s) differ");
    return d;
  }

  // Layout.
  if (!c->layouts.empty() && !r->layouts.empty() && c->layouts != r->layouts) {
    d.reason = CompatibilityReason::IncompatibleLayout;
    d.notes.push_back("tensor layout(s) differ");
    return d;
  }

  // Rank.
  if (c->rank && r->rank && c->rank != r->rank) {
    d.reason = CompatibilityReason::IncompatibleShape;
    d.notes.push_back("tensor rank differs");
    return d;
  }

  // Shape.
  if (!shapes_match(c->shape, r->shape)) {
    bool symbolic_ok = !r->symbolic_shape_constraints.empty();
    if (policy.allow_dynamic_shape_constraints && symbolic_ok) {
      d.reason = CompatibilityReason::CompatibleWithDynamicShapeConstraint;
      d.compatible = true;
      d.notes.push_back("shape differs but request declares symbolic constraints");
      return d;
    }
    d.reason = CompatibilityReason::IncompatibleShape;
    d.notes.push_back("tensor shape differs and no symbolic constraint permits reuse");
    return d;
  }

  // Alignment.
  if (policy.require_exact_alignment && c->alignment_bytes && r->alignment_bytes &&
      c->alignment_bytes != r->alignment_bytes) {
    d.reason = CompatibilityReason::IncompatibleAlignment;
    d.notes.push_back("alignment requirement differs");
    return d;
  }

  // Specialization flags.
  if (c->codegen_flags != r->codegen_flags) {
    d.reason = CompatibilityReason::IncompatibleSpecialization;
    d.notes.push_back("codegen/specialization flags differ");
    return d;
  }
  if (c->specialization != r->specialization) {
    d.reason = CompatibilityReason::IncompatibleSpecialization;
    d.notes.push_back("specialization parameters differ");
    return d;
  }

  // Quantization.
  if (!c->quantization_kind.empty() && !r->quantization_kind.empty() &&
      c->quantization_kind != r->quantization_kind) {
    d.reason = CompatibilityReason::IncompatibleQuantization;
    d.notes.push_back("quantization kind differs");
    return d;
  }

  // Remaining difference is a runtime-revalidatable nuance.
  if (policy.allow_runtime_validation) {
    d.reason = CompatibilityReason::CompatibleWithRuntimeValidation;
    d.compatible = true;
    d.notes.push_back("only runtime-revalidatable attributes differ");
    return d;
  }

  d.reason = CompatibilityReason::IncompatibleSpecialization;
  d.notes.push_back("keys differ beyond the evaluated compatibility dimensions");
  return d;
}

int compatibility_distance(const KernelCompatibilityDecision& d) {
  if (d.reason == CompatibilityReason::ExactCompatible) return 0;
  if (d.reason == CompatibilityReason::CompatibleWithDynamicShapeConstraint) return 1;
  if (d.reason == CompatibilityReason::CompatibleWithRuntimeValidation) return 2;
  return 100;
}

}  // namespace kernelcache
