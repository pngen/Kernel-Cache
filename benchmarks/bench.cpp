// bench.cpp - measured benchmark suite.
#include "kernelcache/kernelcache.hpp"
#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <filesystem>
#include <atomic>
using namespace kernelcache;
static double ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b){
  return std::chrono::duration_cast<std::chrono::microseconds>(b-a).count()/1000.0; }
static KernelCompatibilityKey key(std::int64_t s){ KernelCompatibilityKey k; k.operation="vec_add"; k.artifact_format="kc-synth";
  k.vendor=DeviceVendor::CPU; k.family=AcceleratorFamily::CPU; k.arch="x86_64"; k.runtime_backend="cpu-synth";
  k.compiler_name="msvc"; k.compiler_version_major=19; k.compiler_version_minor=44; k.abi_name="kc-synth-1.0"; k.abi_version=1;
  k.kernel_interface_signature="void(float*,const float*,uint64)"; k.dtypes={Datatype::F32}; k.layouts={TensorLayout::RowMajor};
  k.rank=1; k.shape={s}; k.alignment_bytes=16; k.block_x=256; k.launch_abi="grid-block"; k.finalize(); return k; }
int main(){
  std::string root=std::filesystem::temp_directory_path().string()+"/kc_bench_"+std::to_string(std::rand());
  KernelCacheConfig cfg; cfg.persistence_root=root; cfg.persistence_enabled=true;
  KernelCache cache(cfg); cache.use_builtin_backends();
  auto t0=std::chrono::steady_clock::now(); std::uint64_t sz=0;
  for (int i=0;i<200000;++i){ KernelCompatibilityKey k=key(1024); sz+=k.digest_hex().size(); }
  auto t1=std::chrono::steady_clock::now();
  std::cout<<"key_canonicalization+sha256: "<<ms(t0,t1)<<" ms (200k) -> "<<(200000.0/(ms(t0,t1)/1000.0))<<" ops/s\n";
  KernelLookupRequest req; req.key=key(4096); req.device.vendor=DeviceVendor::CPU;
  req.desired_tier=ResidencyTier::HostResident; req.allow_miss_build=true;
  (void)cache.acquire(req);
  auto t2=std::chrono::steady_clock::now(); auto c=cache.build(req); auto t3=std::chrono::steady_clock::now();
  std::cout<<"cold_build_validate: "<<(c.ok()?ms(t2,t3):0.0)<<" ms\n";
  auto t4=std::chrono::steady_clock::now();
  for (int i=0;i<200000;++i){ auto r=cache.lookup(req); (void)r; }
  auto t5=std::chrono::steady_clock::now();
  std::cout<<"warm_lookup_hit: "<<ms(t4,t5)<<" ms (200k) -> "<<(200000.0/(ms(t4,t5)/1000.0))<<" lookups/s\n";
  KernelCompatibilityKey kf=key(8192);
  auto t6=std::chrono::steady_clock::now();
  std::vector<std::thread> ts(8); std::atomic<int> okc{0};
  for (auto& t : ts) t=std::thread([&]{ KernelLookupRequest r; r.key=kf; r.device.vendor=DeviceVendor::CPU;
    r.desired_tier=ResidencyTier::HostResident; r.allow_miss_build=true; auto l=cache.acquire(r); if(l.ok()){ okc++; l.value().release(); } });
  for (auto& t : ts) t.join();
  auto t7=std::chrono::steady_clock::now();
  std::cout<<"single_flight_8thr: "<<ms(t6,t7)<<" ms (acquired="<<okc<<")\n";
  auto t8=std::chrono::steady_clock::now();
  for (int i=0;i<200;++i){ KernelLookupRequest r; r.key=key(1024+i); r.device.vendor=DeviceVendor::CPU;
    r.desired_tier=ResidencyTier::HostResident; r.allow_miss_build=true; auto l=cache.acquire(r); l.value().release(); }
  auto t9=std::chrono::steady_clock::now();
  std::cout<<"persist_200_artifacts: "<<ms(t8,t9)<<" ms\n";
  std::cout<<"cuda_backend="<<(cuda_backend_available()?"available":"not-available")<<"\n";
  auto t10=std::chrono::steady_clock::now();
  for (int i=0;i<1000;++i){ KernelLookupRequest r; r.key=key((i%10==0)?50000:4096); r.device.vendor=DeviceVendor::CPU;
    r.desired_tier=ResidencyTier::HostResident; r.allow_miss_build=true; auto l=cache.acquire(r); if(l.ok()) l.value().release(); }
  auto t11=std::chrono::steady_clock::now();
  std::cout<<"mixed_90_10: "<<ms(t10,t11)<<" ms (1000 reqs)\n";
  auto st=cache.stats();
  std::cout<<"stats: lookups="<<st.lookups<<" builds="<<st.builds<<" hits="<<(st.exact_hits+st.compatible_hits+st.host_hits+st.device_hits+st.persistent_hits)
      <<" misses="<<st.misses<<" avoid_compile_count="<<st.avoided_compile_count<<"\n";
  std::filesystem::remove_all(root);
  std::cout<<"BENCH_OK\n";
  return 0;
}
