// example 11: expose cost-aware eviction candidates.
#include "kernelcache/kernelcache.hpp"
#include <iostream>
using namespace kernelcache;
KernelCompatibilityKey key(std::int64_t s){ KernelCompatibilityKey k; k.operation="vec_add"; k.artifact_format="kc-synth";
  k.vendor=DeviceVendor::CPU; k.family=AcceleratorFamily::CPU; k.arch="x86_64"; k.runtime_backend="cpu-synth";
  k.compiler_name="msvc"; k.compiler_version_major=19; k.compiler_version_minor=44; k.abi_name="kc-synth-1.0"; k.abi_version=1;
  k.kernel_interface_signature="void(float*,const float*,uint64)"; k.dtypes={Datatype::F32}; k.layouts={TensorLayout::RowMajor};
  k.rank=1; k.shape={s}; k.alignment_bytes=16; k.block_x=256; k.launch_abi="grid-block"; k.finalize(); return k; }
int main(){
  KernelCacheConfig cfg; KernelCache cache(cfg); cache.use_builtin_backends();
  for (auto s : {256,512,1024,2048,4096}){ KernelLookupRequest r; r.key=key(s); r.device.vendor=DeviceVendor::CPU; r.desired_tier=ResidencyTier::HostResident; r.allow_miss_build=true; auto l=cache.acquire(r); l.value().release(); }
  auto cand=cache.eviction_candidates(5);
  std::cout<<"candidates="<<cand.value().size()<<"\n";
  for (auto& c : cand.value()) std::cout<<"  score="<<c.score.total<<" tier="<<c.tier<<"\n";
  return 0;
}