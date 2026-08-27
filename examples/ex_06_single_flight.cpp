// example 06: concurrent same-key single-flight build.
#include "kernelcache/kernelcache.hpp"
#include <iostream>
#include <thread>
#include <vector>
using namespace kernelcache;
KernelCompatibilityKey key(){ KernelCompatibilityKey k; k.operation="vec_add"; k.artifact_format="kc-synth";
  k.vendor=DeviceVendor::CPU; k.family=AcceleratorFamily::CPU; k.arch="x86_64"; k.runtime_backend="cpu-synth";
  k.compiler_name="msvc"; k.compiler_version_major=19; k.compiler_version_minor=44; k.abi_name="kc-synth-1.0"; k.abi_version=1;
  k.kernel_interface_signature="void(float*,const float*,uint64)"; k.dtypes={Datatype::F32}; k.layouts={TensorLayout::RowMajor};
  k.rank=1; k.shape={1024}; k.alignment_bytes=16; k.block_x=256; k.launch_abi="grid-block"; k.finalize(); return k; }
int main(){
  KernelCache cache(KernelCacheConfig{}); cache.use_builtin_backends();
  std::vector<std::thread> ts(16);
  std::atomic<int> ok{0};
  for (auto& t : ts) t=std::thread([&]{ KernelLookupRequest r; r.key=key(); r.device.vendor=DeviceVendor::CPU;
    r.desired_tier=ResidencyTier::HostResident; r.allow_miss_build=true; auto l=cache.acquire(r); if(l.ok()){ ok++; l.value().release(); } });
  for (auto& t : ts) t.join();
  std::cout<<"acquired="<<ok<<" builds="<<cache.stats().builds<<" dedup="<<cache.stats().build_dedup<<"\n";
  return 0;
}