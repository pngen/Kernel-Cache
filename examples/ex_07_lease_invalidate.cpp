// example 07: lease + safe invalidation (drains).
#include "kernelcache/kernelcache.hpp"
#include <iostream>
using namespace kernelcache;
KernelCompatibilityKey key(){ KernelCompatibilityKey k; k.operation="vec_add"; k.artifact_format="kc-synth";
  k.vendor=DeviceVendor::CPU; k.family=AcceleratorFamily::CPU; k.arch="x86_64"; k.runtime_backend="cpu-synth";
  k.compiler_name="msvc"; k.compiler_version_major=19; k.compiler_version_minor=44; k.abi_name="kc-synth-1.0"; k.abi_version=1;
  k.kernel_interface_signature="void(float*,const float*,uint64)"; k.dtypes={Datatype::F32}; k.layouts={TensorLayout::RowMajor};
  k.rank=1; k.shape={1024}; k.alignment_bytes=16; k.block_x=256; k.launch_abi="grid-block"; k.finalize(); return k; }
int main(){
  KernelCache cache(KernelCacheConfig{}); cache.use_builtin_backends();
  KernelLookupRequest r; r.key=key(); r.device.vendor=DeviceVendor::CPU; r.desired_tier=ResidencyTier::HostResident; r.allow_miss_build=true;
  auto l=cache.acquire(r); auto id=l.value().artifact_id();
  cache.invalidate(InvalidationRequest{InvalidationTarget::ArtifactId, id.lo(), id.hi()});
  auto lk=cache.lookup(r); std::cout<<"after invalidation hit="<<lk.value().is_hit()<<"\n";
  l.value().release();
  std::cout<<"done\n"; return 0;
}