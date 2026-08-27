// example 02: miss then single-flight build.
#include "kernelcache/kernelcache.hpp"
#include <iostream>
using namespace kernelcache;
KernelCompatibilityKey key(){ KernelCompatibilityKey k; k.operation="vec_mul"; k.artifact_format="kc-synth";
  k.vendor=DeviceVendor::CPU; k.family=AcceleratorFamily::CPU; k.arch="x86_64"; k.runtime_backend="cpu-synth";
  k.compiler_name="msvc"; k.compiler_version_major=19; k.compiler_version_minor=44; k.abi_name="kc-synth-1.0"; k.abi_version=1;
  k.kernel_interface_signature="void(float*,const float*,uint64)"; k.dtypes={Datatype::F32}; k.layouts={TensorLayout::RowMajor};
  k.rank=1; k.shape={1024}; k.alignment_bytes=16; k.block_x=256; k.launch_abi="grid-block"; k.finalize(); return k; }
int main(){
  KernelCache cache(KernelCacheConfig{}); cache.use_builtin_backends();
  KernelLookupRequest req; req.key=key(); req.device.vendor=DeviceVendor::CPU;
  req.desired_tier=ResidencyTier::HostResident; req.allow_miss_build=true;
  auto b=cache.build(req); std::cout<<"build: "<<(b.ok()?"ok":"err")<<" id="<<(b.ok()?b.value().id.str():"-")<<"\n";
  return b.ok()?0:1;
}