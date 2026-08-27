// persistence_recovery.cpp - crash-safe persistence + recovery under failure.
#include "kernelcache/kernelcache.hpp"
#include "test_util.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <random>

using namespace kernelcache;

static std::string tmp_root() {
#ifdef _WIN32
  return std::filesystem::temp_directory_path().string() + "/kc_pers_" + std::to_string(kc_pid());
#else
  return std::filesystem::temp_directory_path().string() + "/kc_pers_" + std::to_string(getpid());
#endif
}

static KernelCompatibilityKey kk(const std::string& op) {
  KernelCompatibilityKey k;
  k.operation = op; k.artifact_format = "kc-synth";
  k.vendor = DeviceVendor::CPU; k.family = AcceleratorFamily::CPU; k.arch = "x86_64";
  k.runtime_backend = "cpu-synth"; k.compiler_name = "msvc"; k.compiler_version_major = 19; k.compiler_version_minor = 44;
  k.abi_name = "kc-synth-1.0"; k.abi_version = 1; k.kernel_interface_signature = "void(float*,const float*,uint64)";
  k.dtypes = {Datatype::F32}; k.layouts = {TensorLayout::RowMajor}; k.rank = 1; k.shape = {256};
  k.alignment_bytes = 16; k.block_x = 256; k.launch_abi = "grid-block"; k.finalize();
  return k;
}

int main() {
  std::string root = tmp_root();
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  FilePersistenceStore store;
  auto op = store.open(root);
  CHECK_TRUE(op.ok());

  // One valid artifact.
  StoredArtifact A; A.id = ArtifactId(1, 10); A.generation.value = 1; A.key = kk("opA");
  A.format = "kc-synth"; A.namespace_ = "ns"; A.provenance.producer = "cpu-synth";
  A.sizes.artifact_bytes = 64; A.sizes.host_bytes = 64; A.sizes.storage_bytes = 64;
  A.bytes = std::vector<std::uint8_t>(64, 0xAB);
  A.sha256_hex = to_hex(Sha256::digest(std::span<const std::uint8_t>(A.bytes.data(), A.bytes.size())));
  A.stored_ns = 1000;
  CHECK_TRUE(store.put(A).ok());

  // A second valid artifact (restored correctly).
  StoredArtifact B; B.id = ArtifactId(2, 20); B.generation.value = 2; B.key = kk("opB");
  B.format = "kc-synth"; B.namespace_ = "ns"; B.provenance.producer = "cpu-synth";
  B.sizes.artifact_bytes = 32; B.sizes.host_bytes = 32; B.sizes.storage_bytes = 32;
  B.bytes = std::vector<std::uint8_t>(32, 0xCD);
  B.sha256_hex = to_hex(Sha256::digest(std::span<const std::uint8_t>(B.bytes.data(), B.bytes.size())));
  B.stored_ns = 2000;
  CHECK_TRUE(store.put(B).ok());

  // get roundtrip.
  auto gA = store.get(A.id);
  if (!gA.ok()) std::cerr << "getA err: " << gA.error().message() << "\n";
  CHECK_TRUE(gA.ok() && gA.value().bytes.size() == 64);
  CHECK_EQ(gA.value().bytes[0], 0xAB);

  // Corrupt A's blob -> checksum must fail.
  {
    std::ofstream f(root + "/artifacts/" + A.id.str() + ".blob", std::ios::binary | std::ios::trunc);
    std::vector<std::uint8_t> junk(64, 0xFF); f.write(reinterpret_cast<const char*>(junk.data()), 64);
  }
  auto gA2 = store.get(A.id);
  CHECK_TRUE(!gA2.ok());
  CHECK_EQ(gA2.error().code(), ErrorCode::ChecksumMismatch);

  // Truncate B's blob -> length mismatch.
  {
    std::ofstream f(root + "/artifacts/" + B.id.str() + ".blob", std::ios::binary | std::ios::trunc);
    std::vector<std::uint8_t> junk(10, 0x11); f.write(reinterpret_cast<const char*>(junk.data()), 10);
  }
  auto gB2 = store.get(B.id);
  CHECK_TRUE(!gB2.ok());
  CHECK_EQ(gB2.error().code(), ErrorCode::TruncatedData);

  // Unknown metadata version.
  {
    std::ofstream f(root + "/artifacts/deadbeef00000000000000000000dead.meta", std::ios::binary | std::ios::trunc);
    f << "KCMETA9\nid=deadbeef00000000000000000000dead\n";
  }
  // Orphan temp file.
  {
    std::ofstream f(root + "/tmp/orphan.tmp", std::ios::binary | std::ios::trunc);
    f << "partial";
  }

  std::vector<std::string> rejected, orphans;
  auto rec = store.recover(&rejected, &orphans);
  CHECK_TRUE(rec.ok());
  // A and B are corrupt/truncated -> rejected, not returned valid.
  CHECK_TRUE(rec.value().empty());
  CHECK_TRUE(!rejected.empty());
  CHECK_TRUE(!orphans.empty());   // orphan temp cleaned/recorded

  // Persistence from the cache: build a valid artifact, close, reopen, recover.
  std::string cache_root = tmp_root() + "_cache";
  std::filesystem::remove_all(cache_root, ec);
  {
    KernelCacheConfig cfg; cfg.persistence_root = cache_root; cfg.persistence_enabled = true;
    KernelCache cache(cfg); cache.use_builtin_backends();
    KernelLookupRequest req; req.key = kk("opC"); req.device.vendor = DeviceVendor::CPU;
    req.desired_tier = ResidencyTier::HostResident; req.allow_miss_build = true;
    auto a = cache.acquire(req); CHECK_TRUE(a.ok());
    a.value().release();
  }
  {
    // Reopen and recover.
    KernelCacheConfig cfg; cfg.persistence_root = cache_root; cfg.persistence_enabled = true;
    KernelCache cache(cfg); cache.use_builtin_backends();
    auto rc = cache.recover();
    CHECK_TRUE(rc.ok());
    CHECK_TRUE(rc.value() >= 1);
    // The recovered artifact should be a persisted hit.
    KernelLookupRequest req; req.key = kk("opC"); req.device.vendor = DeviceVendor::CPU;
    req.desired_tier = ResidencyTier::HostResident; req.allow_miss_build = false;
    auto h = cache.lookup(req);
    CHECK_TRUE(h.ok());
    CHECK_EQ(h.value().outcome, LookupOutcome::PersistedHit);
  }

  std::filesystem::remove_all(root, ec);
  std::filesystem::remove_all(cache_root, ec);
  std::cout << "PETERS_RECOVERY_OK checks=" << kctest::g_checks << " rejected=" << rejected.size() << " orphans=" << orphans.size() << "\n";
  return 0;
}