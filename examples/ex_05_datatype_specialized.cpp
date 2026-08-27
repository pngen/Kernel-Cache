// example 05: datatype-specialized artifacts.
#include "kernelcache/kernelcache.hpp"
#include <iostream>
using namespace kernelcache;
static KernelCompatibilityKey key(Datatype dt){ KernelCompatibilityKey k; k.operation="vec_add"; k.artifact_format="kc-synth";
  k.vendor=DeviceVendor::CPU; k.family=AcceleratorFamily::CPU; k.arch="x86_64"; k.runtime_backend="cpu-synth";
  k.compiler_name="msvc"; k.compiler_version_major=19; k.compiler_version_minor=44; k.abi_name="kc-synth-1.0"; k.abi_version=1;
  k.kernel_interface_signature="void(float*,const float*,uint64)"; k.dtypes={dt}; k.layouts={TensorLayout::RowMajor};
  k.rank=1; k.shape={1024}; k.alignment_bytes=16; k.block_x=256; k.launch_abi="grid-block"; k.finalize(); return k; }
int main(){
  KernelCache cache(KernelCacheConfig{}); cache.use_builtin_backends();
  for (auto dt : {Datatype::F32, Datatype::F16}){ KernelLookupRequest r; r.key=key(dt); r.device.vendor=DeviceVendor::CPU; r.desired_tier=ResidencyTier::HostResident; r.allow_miss_build=true;
    auto l=cache.acquire(r); if(l.ok()){ std::cout<<datatype_name(dt)<<" -> "<<l.value().artifact_id().str().substr(0,8)<<"\n"; l.value().release(); } }
  return 0;
}