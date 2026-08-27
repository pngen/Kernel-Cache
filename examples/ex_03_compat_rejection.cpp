// example 03: incompatible shape/dtype is never returned as a hit.
#include "kernelcache/kernelcache.hpp"
#include <iostream>
using namespace kernelcache;
static KernelCompatibilityKey key(std::int64_t shape){ KernelCompatibilityKey k; k.operation="vec_add"; k.artifact_format="kc-synth";
  k.vendor=DeviceVendor::CPU; k.family=AcceleratorFamily::CPU; k.arch="x86_64"; k.runtime_backend="cpu-synth";
  k.compiler_name="msvc"; k.compiler_version_major=19; k.compiler_version_minor=44; k.abi_name="kc-synth-1.0"; k.abi_version=1;
  k.kernel_interface_signature="void(float*,const float*,uint64)"; k.dtypes={Datatype::F32}; k.layouts={TensorLayout::RowMajor};
  k.rank=1; k.shape={shape}; k.alignment_bytes=16; k.block_x=256; k.launch_abi="grid-block"; k.finalize(); return k; }
int main(){
  KernelCache cache(KernelCacheConfig{}); cache.use_builtin_backends();
  KernelLookupRequest r1; r1.key=key(1024); r1.device.vendor=DeviceVendor::CPU; r1.desired_tier=ResidencyTier::HostResident; r1.allow_miss_build=true;
  (void)cache.acquire(r1);
  KernelLookupRequest r2; r2.key=key(2048); r2.device.vendor=DeviceVendor::CPU; r2.desired_tier=ResidencyTier::HostResident; r2.allow_miss_build=false;
  auto h=cache.lookup(r2); std::cout<<"shape2048 hit="<<h.value().is_hit()<<" outcome="<<lookup_outcome_name(h.value().outcome)<<"\n";
  return h.value().is_hit()?1:0;
}