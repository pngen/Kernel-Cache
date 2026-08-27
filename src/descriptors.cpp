#include "kernelcache/descriptors.hpp"

namespace kernelcache {

const char* datatype_name(Datatype dt) noexcept {
  switch (dt) {
    case Datatype::None: return "none";
    case Datatype::Bool: return "bool";
    case Datatype::I8: return "i8";
    case Datatype::U8: return "u8";
    case Datatype::I16: return "i16";
    case Datatype::U16: return "u16";
    case Datatype::I32: return "i32";
    case Datatype::U32: return "u32";
    case Datatype::I64: return "i64";
    case Datatype::U64: return "u64";
    case Datatype::F16: return "f16";
    case Datatype::BF16: return "bf16";
    case Datatype::F32: return "f32";
    case Datatype::F64: return "f64";
    case Datatype::TF32: return "tf32";
    case Datatype::Int4: return "int4";
    case Datatype::UInt4: return "uint4";
  }
  return "unknown";
}

std::uint32_t datatype_size_bytes(Datatype dt) noexcept {
  switch (dt) {
    case Datatype::Bool: return 1;
    case Datatype::I8: case Datatype::U8: return 1;
    case Datatype::I16: case Datatype::U16: case Datatype::F16: case Datatype::BF16: return 2;
    case Datatype::I32: case Datatype::U32: case Datatype::F32: case Datatype::TF32: return 4;
    case Datatype::I64: case Datatype::U64: case Datatype::F64: return 8;
    case Datatype::Int4: case Datatype::UInt4: return 1;
    case Datatype::None: return 0;
  }
  return 0;
}

const char* layout_name(TensorLayout layout) noexcept {
  switch (layout) {
    case TensorLayout::None: return "none";
    case TensorLayout::RowMajor: return "row-major";
    case TensorLayout::ColMajor: return "col-major";
    case TensorLayout::NCHW: return "nchw";
    case TensorLayout::NHWC: return "nhwc";
    case TensorLayout::NCWH: return "ncwh";
    case TensorLayout::Blocked: return "blocked";
    case TensorLayout::Strided: return "strided";
    case TensorLayout::Sparse: return "sparse";
  }
  return "unknown";
}

const char* vendor_name(DeviceVendor v) noexcept {
  switch (v) {
    case DeviceVendor::Unknown: return "unknown";
    case DeviceVendor::NVIDIA: return "nvidia";
    case DeviceVendor::AMD: return "amd";
    case DeviceVendor::Intel: return "intel";
    case DeviceVendor::CPU: return "cpu";
  }
  return "unknown";
}

const char* residency_tier_name(ResidencyTier t) noexcept {
  switch (t) {
    case ResidencyTier::None: return "none";
    case ResidencyTier::MetadataOnly: return "metadata-only";
    case ResidencyTier::PersistentStorage: return "persistent";
    case ResidencyTier::HostResident: return "host-resident";
    case ResidencyTier::DeviceResident: return "device-resident";
  }
  return "unknown";
}

const char* artifact_state_name(ArtifactState s) noexcept {
  switch (s) {
    case ArtifactState::Discovered: return "discovered";
    case ArtifactState::Building: return "building";
    case ArtifactState::Built: return "built";
    case ArtifactState::Validating: return "validating";
    case ArtifactState::Valid: return "valid";
    case ArtifactState::Loading: return "loading";
    case ArtifactState::ResidentHost: return "resident-host";
    case ArtifactState::ResidentDevice: return "resident-device";
    case ArtifactState::Persisted: return "persisted";
    case ArtifactState::Leasing: return "leasing";
    case ArtifactState::InUse: return "in-use";
    case ArtifactState::DemotionPending: return "demotion-pending";
    case ArtifactState::EvictionPending: return "eviction-pending";
    case ArtifactState::EvictedDevice: return "evicted-device";
    case ArtifactState::EvictedHost: return "evicted-host";
    case ArtifactState::Invalidated: return "invalidated";
    case ArtifactState::Corrupt: return "corrupt";
    case ArtifactState::Failed: return "failed";
    case ArtifactState::Retired: return "retired";
    case ArtifactState::Terminal: return "terminal";
  }
  return "unknown";
}

}  // namespace kernelcache
