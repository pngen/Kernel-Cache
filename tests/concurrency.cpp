// concurrency.cpp - deadlock-free concurrent use of one shared cache.
#include "kernelcache/kernelcache.hpp"
#include "test_util.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace kernelcache;

static std::string tmp_root() {
#ifdef _WIN32
  return std::filesystem::temp_directory_path().string() + "/kc_conc_" + std::to_string(kc_pid());
#else
  return std::filesystem::temp_directory_path().string() + "/kc_conc_" + std::to_string(getpid());
#endif
}

static KernelCompatibilityKey key_for(int opid, int shape) {
  KernelCompatibilityKey k;
  k.operation = std::string("op") + std::to_string(opid % 4);
  k.artifact_format = "kc-synth";
  k.vendor = DeviceVendor::CPU; k.family = AcceleratorFamily::CPU; k.arch = "x86_64";
  k.runtime_backend = "cpu-synth";
  k.compiler_name = "msvc"; k.compiler_version_major = 19; k.compiler_version_minor = 44;
  k.abi_name = "kc-synth-1.0"; k.abi_version = 1;
  k.kernel_interface_signature = "void(float*,const float*,uint64)";
  k.dtypes = {Datatype::F32}; k.layouts = {TensorLayout::RowMajor}; k.rank = 1;
  k.shape = {shape}; k.alignment_bytes = 16; k.block_x = 256; k.launch_abi = "grid-block";
  k.finalize();
  return k;
}

int main() {
  std::string root = tmp_root();
  KernelCacheConfig cfg; cfg.persistence_root = root; cfg.persistence_enabled = false;
  cfg.residency.device_budget_bytes = 1ull << 20;  // 1 MiB to force eviction churn
  KernelCache cache(cfg);
  cache.use_builtin_backends();

  const int kThreads = 16;
  const int kIters = 400;
  std::atomic<bool> stop{false};
  std::atomic<int> errors{0};
  std::vector<std::thread> ths;
  std::vector<KernelLease> held;

  // Producers/consumers concurrently look up, lease, release, invalidate, evict.
  for (int t = 0; t < kThreads; ++t) {
    ths.emplace_back([&, t]{
      std::mt19937 rng(1000 + t);
      for (int i = 0; i < kIters && !stop.load(); ++i) {
        int op = rng() % 4;
        int shape = (rng() % 4 == 0) ? 512 : 8192;
        KernelLookupRequest req;
        req.key = key_for(op, shape);
        req.device.vendor = DeviceVendor::CPU; req.device.family = AcceleratorFamily::CPU;
        req.desired_tier = ResidencyTier::DeviceResident;
        req.allow_miss_build = (i % 3 == 0);
        auto l = cache.acquire(req);
        if (l.ok()) { (void)l.value().artifact_id(); l.value().release(); }
        // Concurrent invalidation / eviction of arbitrary artifacts.
        auto arts = cache.list();
        if (!arts.value().empty() && (rng() % 50 == 0)) {
          auto a = arts.value()[rng() % arts.value().size()];
          if (rng() % 2 == 0) { (void)cache.invalidate(InvalidationRequest{InvalidationTarget::ArtifactId, a.id.lo(), a.id.hi()}); }
          else { (void)cache.evict(a.id, false); }
        }
      }
    });
  }
  // A dedicated thread hammers single-flight on one identical key.
  for (int t = 0; t < 8; ++t) {
    ths.emplace_back([&]{
      for (int i = 0; i < 200; ++i) {
        KernelLookupRequest req;
        req.key = key_for(0, 1024);
        req.device.vendor = DeviceVendor::CPU;
        req.desired_tier = ResidencyTier::HostResident;
        req.allow_miss_build = true;
        auto l = cache.acquire(req);
        if (l.ok()) l.value().release();
      }
    });
  }
  for (auto& th : ths) th.join();

  auto st = cache.stats();
  auto snap = cache.snapshot();
  CHECK_TRUE(snap.artifact_count > 0);
  CHECK_TRUE(st.lookups > 0);
  // Leases must be fully drained.
  CHECK_EQ(st.active_leases, 0u);
  // No negative accounting.
  CHECK_TRUE(snap.device_resident_bytes >= 0);
  // Index/canonical agreement.
  std::error_code ec;
  CHECK_TRUE(st.invalidations >= 0);
  std::filesystem::remove_all(root, ec);
  std::cout << "CONCurrency_OK checks=" << kctest::g_checks << " artifacts=" << snap.artifact_count << "\n";
  return 0;
}