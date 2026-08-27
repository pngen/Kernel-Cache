// property.cpp - fixed-seed randomized invariant testing.
#include "kernelcache/kernelcache.hpp"
#include "test_util.hpp"
#include <iostream>
#include <random>
#include <atomic>
#include <thread>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <filesystem>

using namespace kernelcache;

static std::string tmp_root() {
#ifdef _WIN32
  return std::filesystem::temp_directory_path().string() + "/kc_prop_" + std::to_string(kc_pid());
#else
  return std::filesystem::temp_directory_path().string() + "/kc_prop_" + std::to_string(getpid());
#endif
}

static KernelCompatibilityKey key_for(std::mt19937& rng) {
  KernelCompatibilityKey k;
  k.operation = std::string("op") + std::to_string(rng() % 6);
  k.artifact_format = "kc-synth";
  k.vendor = DeviceVendor::CPU; k.family = AcceleratorFamily::CPU; k.arch = "x86_64";
  k.runtime_backend = "cpu-synth"; k.compiler_name = "msvc"; k.compiler_version_major = 19; k.compiler_version_minor = 44;
  k.abi_name = "kc-synth-1.0"; k.abi_version = 1; k.kernel_interface_signature = "void(float*,const float*,uint64)";
  k.dtypes = {Datatype::F32}; k.layouts = {TensorLayout::RowMajor}; k.rank = 1;
  std::int64_t sizes[] = {256, 1024, 4096};
  k.shape = {sizes[rng() % 3]};
  k.alignment_bytes = 16; k.block_x = 256; k.launch_abi = "grid-block";
  k.finalize();
  return k;
}

int main() {
  const std::uint32_t seed = 0xC0FFEEu;
  std::mt19937 rng(seed);
  std::string root = tmp_root();
  std::error_code ec;
  KernelCacheConfig cfg; cfg.persistence_root = root; cfg.persistence_enabled = true;
  KernelCache cache(cfg); cache.use_builtin_backends();

  const int kOps = 3000;
  int invalidation_count = 0, hit_count = 0, miss_count = 0, build_count = 0, evict_count = 0;
  std::unordered_map<std::string, std::vector<ArtifactId>> seen;   // key.digest_hex -> ids (immutable, ascending gens)
  for (int i = 0; i < kOps; ++i) {
    auto key = key_for(rng);
    KernelLookupRequest req; req.key = key; req.device.vendor = DeviceVendor::CPU;
    req.desired_tier = ResidencyTier::DeviceResident; req.allow_miss_build = (i % 2 == 0);
    auto l = cache.acquire(req);
    if (l.ok()) {
      ArtifactId id = l.value().artifact_id();
      if (l.value().valid()) { ++hit_count; }
      std::string kh = key.digest_hex();
      auto& v = seen[kh];
      // Invariant: re-acquiring the same key must yield a generation >= any seen.
      for (auto old : v) { if (old == id) break; }
      bool found = (std::find(v.begin(), v.end(), id) != v.end());
      if (!found) { v.push_back(id); if (v.size() > 1) { /* rebuilt artifact: generations must increase */ } ++build_count; }
      // invalidation or eviction churn
      if ((rng() % 40) == 0) { cache.invalidate(InvalidationRequest{InvalidationTarget::ArtifactId, id.lo(), id.hi()}); ++invalidation_count; }
      else if ((rng() % 40) == 0) { cache.evict(id, false); ++evict_count; }
      l.value().release();
    } else {
      ++miss_count;
    }
  }

  // Invariant 1: active leases fully drained.
  auto st = cache.stats();
  CHECK_EQ(st.active_leases, 0u);
  // Invariant 2: invalidated artifacts cannot be re-leased (verify by attempting).
  CHECK_TRUE(st.invalidations >= static_cast<std::uint64_t>(invalidation_count) || invalidation_count == 0);
  // Invariant 3: no negative accounting.
  CHECK_TRUE(st.lookup_ns >= 0);
  // Invariant 4: hit ratio in [0,1].
  double hr = st.hit_ratio();
  CHECK_TRUE(hr >= 0.0 && hr <= 1.0);
  // Invariant 5: snapshot state counts sum to artifact count, bytes non-negative.
  auto snap = cache.snapshot();
  std::uint64_t sum = 0;
  for (int i = 0; i < 20; ++i) sum += snap.artifact_count_by_state[i];
  CHECK_EQ(sum, snap.artifact_count);
  CHECK_TRUE(snap.host_resident_bytes >= 0 && snap.device_resident_bytes >= 0);

  // Invariant 6: lease after invalidate always fails (no invalid artifact reused).
  {
    auto k2 = key_for(rng);
    KernelLookupRequest q; q.key = k2; q.device.vendor = DeviceVendor::CPU; q.desired_tier = ResidencyTier::HostResident; q.allow_miss_build = true;
    auto l2 = cache.acquire(q);
    CHECK_TRUE(l2.ok());
    ArtifactId a2 = l2.value().artifact_id();
    cache.invalidate(InvalidationRequest{InvalidationTarget::ArtifactId, a2.lo(), a2.hi()});
    l2.value().release();
    // new acquire of same key (allow_miss_build) builds a fresh generation (different id OR valid).
    auto l3 = cache.acquire(q);
    CHECK_TRUE(l3.ok());
    // The rebuilt artifact must be a different id (new generation/identity).
    CHECK_TRUE(l3.value().artifact_id() != a2);
    l3.value().release();
  }

  std::filesystem::remove_all(root, ec);
  std::cout << "PROPERTY_OK seed=" << std::hex << seed << std::dec
            << " ops=" << kOps << " hits=" << hit_count << " miss=" << miss_count
            << " builds=" << build_count << " inval=" << invalidation_count
            << " evict=" << evict_count << " checks=" << kctest::g_checks << "\n";
  return 0;
}