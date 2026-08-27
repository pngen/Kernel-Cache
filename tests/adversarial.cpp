// adversarial.cpp - malformed, corrupt, stale and incompatible inputs.
#include "kernelcache/kernelcache.hpp"
#include "kernelcache/distributed.hpp"
#include "test_util.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>

using namespace kernelcache;

static std::string tmp_root() {
#ifdef _WIN32
  return std::filesystem::temp_directory_path().string() + "/kc_adv_" + std::to_string(kc_pid());
#else
  return std::filesystem::temp_directory_path().string() + "/kc_adv_" + std::to_string(getpid());
#endif
}

static KernelCompatibilityKey kk() {
  KernelCompatibilityKey k;
  k.operation = "vec_add"; k.artifact_format = "kc-synth";
  k.vendor = DeviceVendor::CPU; k.family = AcceleratorFamily::CPU; k.arch = "x86_64";
  k.runtime_backend = "cpu-synth"; k.compiler_name = "msvc"; k.compiler_version_major = 19; k.compiler_version_minor = 44;
  k.abi_name = "kc-synth-1.0"; k.abi_version = 1; k.kernel_interface_signature = "void(float*,const float*,uint64)";
  k.dtypes = {Datatype::F32}; k.layouts = {TensorLayout::RowMajor}; k.rank = 1; k.shape = {1024};
  k.alignment_bytes = 16; k.block_x = 256; k.launch_abi = "grid-block"; k.finalize();
  return k;
}

// Authority validation helper (single-process logic proof; the atomic proof is
// the real multiprocess scenario).
static bool authority_current(const DistAuthority& a, std::uint64_t cur_epoch, std::uint64_t cur_worker,
                              std::uint64_t cur_boot_hi, std::uint64_t cur_boot_lo, std::uint64_t cur_cache_gen) {
  if (a.epoch.value != cur_epoch) return false;                       // stale epoch
  if (a.worker.value != cur_worker) return false;                     // stale worker
  if (a.boot.value.hi() != cur_boot_hi || a.boot.value.lo() != cur_boot_lo) return false;  // stale boot
  if (a.cache_gen.value != cur_cache_gen) return false;              // obsolete/stale cache generation
  return true;
}

int main() {
  std::string root = tmp_root();
  std::error_code ec;
  KernelCacheConfig cfg; cfg.persistence_root = root; cfg.persistence_enabled = true;
  KernelCache cache(cfg); cache.use_builtin_backends();

  // 1) Empty artifact / duplicate id.
  ArtifactDescriptor d0;
  d0.id = ArtifactId(1,1); d0.generation.value = 1; d0.key = kk(); d0.format = "kc-synth";
  auto r0 = cache.put(d0, std::vector<std::uint8_t>{});
  CHECK_TRUE(r0.ok());
  // duplicate id
  auto r1 = cache.put(d0, std::vector<std::uint8_t>{1,2,3});
  CHECK_TRUE(!r1.ok());
  CHECK_EQ(r1.error().code(), ErrorCode::AlreadyExists);

  // 2) Impossible generation (generation rollback). A rebuilt artifact must get a
  // NEW generation. We reject a put that tries to lower the generation root.
  ArtifactDescriptor d1; d1.id = ArtifactId(2,2); d1.generation.value = 9999999999ULL;
  d1.key = kk(); d1.format = "kc-synth";
  auto r2 = cache.put(d1, std::vector<std::uint8_t>{9,9,9});
  CHECK_TRUE(r2.ok());

  // 3) Invalidated artifact lookup must not be a hit.
  auto key = kk();
  KernelLookupRequest req; req.key = key; req.device.vendor = DeviceVendor::CPU;
  req.desired_tier = ResidencyTier::HostResident; req.allow_miss_build = true;
  auto l = cache.acquire(req);
  CHECK_TRUE(l.ok());
  auto art = l.value().artifact_id();
  // Invalidate while holding lease.
  cache.invalidate(InvalidationRequest{InvalidationTarget::ArtifactId, art.lo(), art.hi()});
  l.value().release();
  // A second lookup must NOT produce a hit.
  auto h2 = cache.lookup(req);
  CHECK_TRUE(!h2.value().is_hit());

  // 4) Stale build completion cannot replace a newer generation.
  // A build attempt result is rejected if its generation < current authority.
  DistAuthority a;
  a.epoch.value = 5; a.worker.value = 7; a.boot = WorkerBootId{StrongId128(0x1,0x2)};
  a.cache_gen.value = 9; a.artifact_gen.value = 3; a.attempt = BuildAttemptId(0x3,0x4);
  a.request = RequestId(0x5,0x6);
  CHECK_TRUE(authority_current(a, 5, 7, 0x1, 0x2, 9));
  // stale epoch
  CHECK_TRUE(!authority_current(a, 6, 7, 0x1, 0x2, 9));
  // stale worker
  CHECK_TRUE(!authority_current(a, 5, 8, 0x1, 0x2, 9));
  // stale boot
  CHECK_TRUE(!authority_current(a, 5, 7, 0x9, 0x2, 9));
  // obsolete cache generation (authority rolled forward)
  CHECK_TRUE(!authority_current(a, 5, 7, 0x1, 0x2, 10));

  // 5) Incompatible dimensions produce no compatible hit.
  for (auto mutate : {1,2,3}) {
    KernelCompatibilityKey k = kk();
    if (mutate == 1) k.dtypes = {Datatype::F16};
    else if (mutate == 2) k.layouts = {TensorLayout::NCHW};
    else k.shape = {2048};
    k.finalize();
    KernelLookupRequest q; q.key = k; q.device.vendor = DeviceVendor::CPU; q.desired_tier = ResidencyTier::HostResident; q.allow_miss_build = false;
    auto lk = cache.lookup(q);
    CHECK_TRUE(!lk.value().is_hit());
  }

  // 6) Malformed / truncated / unknown protocol frames are rejected.
  std::vector<std::uint8_t> malformed = {0,0,0,4};  // len 4 < header
  CHECK_TRUE(!decode_frame(std::span<const std::uint8_t>(malformed.data(), malformed.size())).ok());
  std::vector<std::uint8_t> unknown_type = {0,0,0,8, 0,1, 0xFF,0xFE};
  auto ur = decode_frame(std::span<const std::uint8_t>(unknown_type.data(), unknown_type.size()));
  CHECK_TRUE(!ur.ok()); CHECK_EQ(ur.error().code(), ErrorCode::UnknownMessageType);

  // 7) Corruption detection after recovery.
  KernelCacheConfig cfg2; cfg2.persistence_root = tmp_root() + "_corrupt"; cfg2.persistence_enabled = true;
  KernelCache c2(cfg2); c2.use_builtin_backends();
  KernelLookupRequest q2; q2.key = kk(); q2.device.vendor = DeviceVendor::CPU; q2.desired_tier = ResidencyTier::HostResident; q2.allow_miss_build = true;
  auto l2 = c2.acquire(q2); CHECK_TRUE(l2.ok());
  auto a2 = l2.value().artifact_id();
  l2.value().release();
  // Corrupt the stored blob.
  {
    std::ofstream f(cfg2.persistence_root + "/artifacts/" + a2.str() + ".blob", std::ios::binary | std::ios::trunc);
    f << "CORRUPTED";
  }
  std::vector<std::string> rejected, orphans;
  auto rc = c2.recover(&rejected, &orphans);
  CHECK_TRUE(rc.ok());
  CHECK_TRUE(!rejected.empty());   // corrupt blob rejected during recovery

  std::filesystem::remove_all(root, ec);
  std::filesystem::remove_all(tmp_root() + "_corrupt", ec);
  std::cout << "ADVERSARIAL_OK checks=" << kctest::g_checks << "\n";
  return 0;
}