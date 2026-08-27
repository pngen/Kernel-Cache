// example 01: basic cache hit / miss + build.
#include "kernelcache/kernelcache.hpp"
#include <iostream>
using namespace kernelcache;
int main(){
  KernelCacheConfig cfg; cfg.persistence_enabled=false;
  KernelCache cache(cfg); cache.use_builtin_backends();
  KernelCompatibilityKey k; k.operation="vec_add"; k.artifact_format="kc-synth";
  k.vendor=DeviceVendor::CPU; k.family=AcceleratorFamily::CPU; k.arch="x86_64";
  k.runtime_backend="cpu-synth"; k.compiler_name="msvc"; k.compiler_version_major=19; k.compiler_version_minor=44;
  k.abi_name="kc-synth-1.0"; k.abi_version=1; k.kernel_interface_signature="void(float*,const float*,uint64)";
  k.dtypes={Datatype::F32}; k.layouts={TensorLayout::RowMajor}; k.rank=1; k.shape={1024};
  k.alignment_bytes=16; k.block_x=256; k.launch_abi="grid-block"; k.finalize();
  KernelLookupRequest req; req.key=k; req.device.vendor=DeviceVendor::CPU;
  req.desired_tier=ResidencyTier::HostResident; req.allow_miss_build=true;
  auto m0=cache.lookup(req); std::cout<<"first: "<<lookup_outcome_name(m0.value().outcome)<<"\n";
  auto l=cache.acquire(req); std::cout<<"acquire="<<(l.ok()?"ok":"err")<<" id="<<(l.ok()?l.value().artifact_id().str():"-")<<"\n";
  if(l.ok()) l.value().release();
  auto m1=cache.lookup(req); std::cout<<"second: "<<lookup_outcome_name(m1.value().outcome)<<"\n";
  return 0;
}