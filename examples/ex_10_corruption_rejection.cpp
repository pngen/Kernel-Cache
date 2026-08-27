// example 10: corrupt persisted artifact is rejected on recovery.
#include "kernelcache/kernelcache.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
using namespace kernelcache;
KernelCompatibilityKey key(){ KernelCompatibilityKey k; k.operation="vec_add"; k.artifact_format="kc-synth";
  k.vendor=DeviceVendor::CPU; k.family=AcceleratorFamily::CPU; k.arch="x86_64"; k.runtime_backend="cpu-synth";
  k.compiler_name="msvc"; k.compiler_version_major=19; k.compiler_version_minor=44; k.abi_name="kc-synth-1.0"; k.abi_version=1;
  k.kernel_interface_signature="void(float*,const float*,uint64)"; k.dtypes={Datatype::F32}; k.layouts={TensorLayout::RowMajor};
  k.rank=1; k.shape={1024}; k.alignment_bytes=16; k.block_x=256; k.launch_abi="grid-block"; k.finalize(); return k; }
int main(){
  std::string root=std::filesystem::temp_directory_path().string()+"/kc_ex10_"+std::to_string(std::rand());
  KernelCacheConfig cfg; cfg.persistence_root=root; cfg.persistence_enabled=true;
  { KernelCache c(cfg); c.use_builtin_backends(); KernelLookupRequest r; r.key=key(); r.device.vendor=DeviceVendor::CPU; r.desired_tier=ResidencyTier::HostResident; r.allow_miss_build=true; auto l=c.acquire(r); auto id=l.value().artifact_id(); l.value().release();
    std::ofstream f(root+"/artifacts/"+id.str()+".blob", std::ios::binary|std::ios::trunc); f<<"CORRUPT"; }
  KernelCache c(cfg); c.use_builtin_backends(); std::vector<std::string> rej, orph;
  auto rc=c.recover(&rej,&orph); std::cout<<"recovered="<<rc.value()<<" rejected="<<rej.size()<<"\n";
  std::filesystem::remove_all(root); return 0;
}