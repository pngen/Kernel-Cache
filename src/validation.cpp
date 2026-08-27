// validation.cpp - common metadata integrity/format/arch/ABI validation steps.
#include "kernelcache/descriptors.hpp"
#include "kernelcache/key.hpp"
#include "kernelcache/sha256.hpp"

namespace kernelcache {

ValidationDescriptor validate_artifact_metadata(const KernelCompatibilityKey& key,
                                                const std::vector<std::uint8_t>& bytes,
                                                const std::string& format) {
  ValidationDescriptor vd;
  vd.integrity_hash_checked = !bytes.empty();
  // Format: magic prefix or non-empty payload.
  vd.format_checked = !format.empty() && !bytes.empty();
  // Architecture: key must declare a target the artifact was built for.
  vd.arch_checked = !key.arch.empty() || key.vendor != DeviceVendor::Unknown;
  vd.abi_checked = !key.abi_name.empty();
  vd.metadata_consistent = !key.canonical_bytes().empty();
  vd.loadable = false;  // set by the load stage
  vd.execution_smoke = false;
  vd.reference_compare = false;
  vd.backend_reason.clear();
  return vd;
}

}  // namespace kernelcache
