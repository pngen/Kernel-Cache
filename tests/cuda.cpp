// cuda.cpp - real CUDA compile/load/execute/validate on Blackwell/sm_120.
// When the cache is built without CUDA this test skips cleanly.
#include "kernelcache/kernelcache.hpp"
#include "test_util.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <filesystem>

using namespace kernelcache;

static std::string tmp_root() {
#ifdef _WIN32
  return std::filesystem::temp_directory_path().string() + "/kc_cuda_" + std::to_string(kc_pid());
#else
  return std::filesystem::temp_directory_path().string() + "/kc_cuda_" + std::to_string(getpid());
#endif
}

static const char* kVecAddSrc = R"CUDA(
extern "C" __global__ void vec_add(float* out, const float* a, const float* b, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) out[i] = a[i] + b[i];
}
)CUDA";

static KernelCompatibilityKey cuda_key(const std::string& op, const std::vector<std::int64_t>& shape, Datatype dt) {
  KernelCompatibilityKey k;
  k.operation = op;
  k.artifact_format = "cubin";
  k.vendor = DeviceVendor::NVIDIA; k.family = AcceleratorFamily::CUDA; k.arch = "sm_120";
  k.capability = {12, 0};
  k.runtime_backend = "cuda";
  k.runtime_version_major = 13; k.runtime_version_minor = 1;
  k.compiler_name = "nvcc"; k.compiler_version_major = 13; k.compiler_version_minor = 1;
  k.abi_name = "cuda-1.0"; k.abi_version = 1;
  k.kernel_interface_signature = "void(float*,const float*,const float*,int)";
  k.dtypes = {dt}; k.layouts = {TensorLayout::RowMajor}; k.rank = 1; k.shape = shape;
  k.alignment_bytes = 16; k.block_x = 256; k.launch_abi = "grid-block";
  k.finalize();
  return k;
}

int main() {
  if (!cuda_backend_available()) { std::cout << "CUDA_TEST_SKIP (CUDA unavailable)\n"; return 0; }
  std::string root = tmp_root();
  std::error_code ec;
  KernelCacheConfig cfg; cfg.persistence_root = root; cfg.persistence_enabled = true;
  KernelCache cache(cfg); cache.use_builtin_backends();

  std::uint64_t n = 1u << 20;
  auto key = cuda_key("vec_add", {static_cast<std::int64_t>(n)}, Datatype::F32);
  KernelLookupRequest req; req.key = key; req.source = kVecAddSrc;
  req.device.vendor = DeviceVendor::NVIDIA; req.device.family = AcceleratorFamily::CUDA;
  req.device.arch_name = "sm_120"; req.device.capability = {12,0};
  req.desired_tier = ResidencyTier::DeviceResident; req.allow_miss_build = true;

  // Cold build + validation (real nvcc compile + driver load + launch + compare).
  auto lease = cache.acquire(req);
  if (!lease.ok()) { std::cerr << "CUDA acquire: " << lease.error().message() << "\n"; return 1; }
  CHECK_TRUE(lease.ok());
  auto aid = lease.value().artifact_id();
  CHECK_TRUE(aid != ArtifactId{});

  // The validator must have passed (build only publishes validated artifacts).
  auto d = cache.inspect(aid);
  CHECK_TRUE(d.ok());
  CHECK_TRUE(d.value().validation.all_passed());

  // Execute the CUDA kernel through the cache and compare to a CPU reference.
  std::vector<float> a(n), b(n), out(n);
  std::vector<float> ref(n);
  for (std::uint64_t i = 0; i < n; ++i) { a[i] = 0.5f * static_cast<float>(i % 977); b[i] = 0.25f * static_cast<float>(i % 811); ref[i] = a[i] + b[i]; }
  auto h = cache.load(aid);
  CHECK_TRUE(h.ok());
  CHECK_TRUE(h.value().device_resident);
  int ni = static_cast<int>(n);
  void* args[] = { out.data(), a.data(), b.data(), &ni };
  auto ex = cache.execute(h.value(), args, 4 * sizeof(void*));
  CHECK_TRUE(ex.ok());
  bool match = true; for (std::uint64_t i = 0; i < n; ++i) if (std::abs(out[i] - ref[i]) > 1e-3f) { match = false; break; }
  CHECK_TRUE(match);

  // Device eviction + reload, and host/persistent survival.
  auto ev = cache.evict(aid, false);
  CHECK_TRUE(ev.ok() || ev.error().code() == ErrorCode::EvictionForbidden);
  // Re-acquire (reload from persistent).
  lease.value().release();
  auto lease2 = cache.acquire(req);
  CHECK_TRUE(lease2.ok());
  lease2.value().release();

  // Incompatible architecture metadata rejected.
  auto bad = cuda_key("vec_add", {static_cast<std::int64_t>(n)}, Datatype::F32);
  bad.arch = "sm_90"; bad.capability = {9,0}; bad.finalize();
  KernelLookupRequest rbad; rbad.key = bad; rbad.source = kVecAddSrc; rbad.device = req.device;
  rbad.desired_tier = ResidencyTier::DeviceResident; rbad.allow_miss_build = false;
  auto dbad = cache.lookup(rbad);
  CHECK_TRUE(!dbad.value().is_hit());   // sm_90 artifact must not satisfy sm_120 request

  // Incompatible datatype rejected.
  auto fbad = cuda_key("vec_add", {static_cast<std::int64_t>(n)}, Datatype::F16);
  KernelLookupRequest rf; rf.key = fbad; rf.source = kVecAddSrc; rf.device = req.device;
  rf.desired_tier = ResidencyTier::DeviceResident; rf.allow_miss_build = false;
  auto df = cache.lookup(rf);
  CHECK_TRUE(!df.value().is_hit());

  std::filesystem::remove_all(root, ec);
  std::cout << "CUDA_TEST_OK checks=" << kctest::g_checks << "\n";
  return 0;
}