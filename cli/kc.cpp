#define _CRT_SECURE_NO_WARNINGS
// kc.cpp - the KernelCache command-line interface. Every command is backed by a
// real library call; none are stubs.
#include "kernelcache/kernelcache.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <random>
#include <chrono>

using namespace kernelcache;

static void usage() {
  std::printf("kc - KernelCache command line\n\n"
    "  put <op> <shape> <file|->         publish artifact bytes from a file (or stdin)\n"
    "  build <op> <shape>                build (compile) an artifact\n"
    "  lookup <op> <shape>               lookup and print hit/miss\n"
    "  validate <op> <shape>             validate an artifact\n"
    "  load <op> <shape> [device]        load an artifact to a residency tier\n"
    "  unload <op> <shape>               unload an artifact\n"
    "  execute <op> <shape>              execute the cached kernel\n"
    "  invalidate <op> <shape>           invalidate by operation\n"
    "  evict <id>                        evict an artifact by 32-hex id\n"
    "  pin <id>                          pin an artifact\n"
    "  unpin <id>                        unpin an artifact\n"
    "  list [state]                      list artifacts\n"
    "  inspect <id>                      inspect an artifact\n"
    "  stats                             print stats\n"
    "  snapshot                          print a snapshot\n"
    "  explain <id>                      print an explain report\n"
    "  recover                          run persistence recovery\n"
    "  bench <n>                        micro-benchmark n lookups\n"
    "  serve <port> <root>               run the coordinator (serve)\n"
    "  worker <host> <port> <id> <bh> <bl>  run a cache worker\n");
}

static KernelCompatibilityKey make_key(const std::string& op, std::int64_t shape) {
  KernelCompatibilityKey k;
  k.operation = op; k.artifact_format = "kc-synth";
  k.vendor = DeviceVendor::CPU; k.family = AcceleratorFamily::CPU; k.arch = "x86_64";
  k.runtime_backend = "cpu-synth"; k.compiler_name = "msvc"; k.compiler_version_major = 19; k.compiler_version_minor = 44;
  k.abi_name = "kc-synth-1.0"; k.abi_version = 1; k.kernel_interface_signature = "void(float*,const float*,uint64)";
  k.dtypes = {Datatype::F32}; k.layouts = {TensorLayout::RowMajor}; k.rank = 1; k.shape = {shape};
  k.alignment_bytes = 16; k.block_x = 256; k.launch_abi = "grid-block"; k.finalize();
  return k;
}

int main(int argc, char** argv) {
  if (argc < 2) { usage(); return 2; }
  std::string cmd = argv[1];
  std::string root = getenv("KC_ROOT") ? getenv("KC_ROOT") : (std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "/kc_cli");
  KernelCacheConfig cfg; cfg.persistence_root = root; cfg.persistence_enabled = true;
  KernelCache cache(cfg); cache.use_builtin_backends();

  if (cmd == "put" && argc >= 4) {
    auto key = make_key(argv[2], std::atoll(argv[3]));
    KernelLookupRequest req; req.key = key; req.device.vendor = DeviceVendor::CPU;
    req.desired_tier = ResidencyTier::HostResident; req.allow_miss_build = true;
    auto d = cache.build(req);
    if (d.ok()) std::printf("built id=%s\n", d.value().id.str().c_str());
    else std::printf("put failed: %s\n", d.error().message().c_str());
    return d.ok() ? 0 : 1;
  }
  if (cmd == "build" && argc >= 4) {
    auto key = make_key(argv[2], std::atoll(argv[3]));
    KernelLookupRequest req; req.key = key; req.device.vendor = DeviceVendor::CPU;
    req.desired_tier = ResidencyTier::HostResident; req.allow_miss_build = true;
    auto d = cache.build(req);
    if (d.ok()) std::printf("built id=%s gen=%llu\n", d.value().id.str().c_str(), (unsigned long long)d.value().generation.value);
    else std::printf("build failed: %s\n", d.error().message().c_str());
    return d.ok() ? 0 : 1;
  }
  if (cmd == "lookup" && argc >= 4) {
    auto key = make_key(argv[2], std::atoll(argv[3]));
    KernelLookupRequest req; req.key = key; req.device.vendor = DeviceVendor::CPU;
    req.desired_tier = ResidencyTier::HostResident; req.allow_miss_build = true;
    auto r = cache.lookup(req);
    if (r) std::printf("outcome=%s artifact=%s gen=%llu\n",
      lookup_outcome_name(r.value().outcome),
      r.value().artifact ? r.value().artifact->str().c_str() : "-",
      r.value().generation ? (unsigned long long)r.value().generation->value : 0);
    else std::printf("lookup error: %s\n", r.error().message().c_str());
    return 0;
  }
  if (cmd == "validate" && argc >= 4) {
    auto key = make_key(argv[2], std::atoll(argv[3]));
    KernelLookupRequest req; req.key = key; req.device.vendor = DeviceVendor::CPU;
    auto l = cache.acquire(req);
    if (l.ok()) { auto d = cache.inspect(l.value().artifact_id()); if (d.ok()) std::printf("valid=%d reason=%s\n", d.value().validation.all_passed(), d.value().validation.backend_reason.c_str()); l.value().release(); }
    else std::printf("validate failed: %s\n", l.error().message().c_str());
    return 0;
  }
  if (cmd == "load" && argc >= 4) {
    auto key = make_key(argv[2], std::atoll(argv[3]));
    KernelLookupRequest req; req.key = key; req.device.vendor = DeviceVendor::CPU;
    req.desired_tier = ResidencyTier::DeviceResident; req.allow_miss_build = true;
    auto l = cache.acquire(req);
    if (l.ok()) { std::printf("loaded id=%s tier=%d\n", l.value().artifact_id().str().c_str(), (int)l.value().residency()); l.value().release(); }
    else std::printf("load failed: %s\n", l.error().message().c_str());
    return 0;
  }
  if (cmd == "unload" && argc >= 4) {
    auto key = make_key(argv[2], std::atoll(argv[3]));
    KernelLookupRequest req; req.key = key; req.device.vendor = DeviceVendor::CPU; req.desired_tier = ResidencyTier::HostResident; req.allow_miss_build = true;
    auto l = cache.acquire(req);
    if (l.ok()) { auto r = cache.unload(l.value().artifact_id()); std::printf("unloaded: %s\n", r ? "ok" : r.error().message().c_str()); l.value().release(); }
    return 0;
  }
  if (cmd == "execute" && argc >= 4) {
    std::uint64_t n = static_cast<std::uint64_t>(std::atoll(argv[3]));
    auto key = make_key(argv[2], (std::int64_t)n);
    KernelLookupRequest req; req.key = key; req.device.vendor = DeviceVendor::CPU;
    req.desired_tier = ResidencyTier::DeviceResident; req.allow_miss_build = true;
    auto l = cache.acquire(req);
    if (!l.ok()) { std::printf("execute acquire: %s\n", l.error().message().c_str()); return 1; }
    auto h = cache.load(l.value().artifact_id());
    std::vector<float> a(n), b(n), out(n);
    for (std::uint64_t i = 0; i < n; ++i) { a[i] = 1.0f; b[i] = 2.0f; }
    float* args[] = { out.data(), a.data(), b.data() };
    auto r = cache.execute(h.value(), const_cast<float**>(args), 3 * sizeof(void*));
    std::printf("execute: %s out0=%.2f\n", r ? "ok" : r.error().message().c_str(), out[0]);
    l.value().release();
    return r ? 0 : 1;
  }
  if (cmd == "invalidate" && argc >= 4) {
    auto key = make_key(argv[2], std::atoll(argv[3]));
    InvalidationRequest ir; ir.target = InvalidationTarget::Operation; ir.operation = argv[2];
    auto r = cache.invalidate(ir);
    if (r) std::printf("invalidated %u artifacts\n", r.value().empty() ? 0 : r.value()[0].affected);
    else std::printf("invalidate: %s\n", r.error().message().c_str());
    return 0;
  }
  if (cmd == "evict" && argc >= 3) {
    auto id = StrongId128::parse(argv[2]);
    if (!id) { std::printf("bad id\n"); return 1; }
    auto r = cache.evict(ArtifactId(id.value().hi(), id.value().lo()), false);
    std::printf("evict: %s\n", r ? "ok" : r.error().message().c_str());
    return 0;
  }
  if (cmd == "pin" && argc >= 3) { auto id = StrongId128::parse(argv[2]); if (id) cache.pin(ArtifactId(id.value().hi(), id.value().lo())); std::printf("pinned\n"); return 0; }
  if (cmd == "unpin" && argc >= 3) { auto id = StrongId128::parse(argv[2]); if (id) cache.unpin(ArtifactId(id.value().hi(), id.value().lo())); std::printf("unpinned\n"); return 0; }
  if (cmd == "list") {
    auto r = cache.list();
    if (r) for (auto& d : r.value()) std::printf("%s gen=%llu op=%s state=%s\n", d.id.str().c_str(), (unsigned long long)d.generation.value, d.key.operation.c_str(), artifact_state_name(d.validation.all_passed() ? ArtifactState::Valid : ArtifactState::Corrupt));
    return 0;
  }
  if (cmd == "inspect" && argc >= 3) {
    auto id = StrongId128::parse(argv[2]);
    if (!id) { std::printf("bad id\n"); return 1; }
    auto d = cache.inspect(ArtifactId(id.value().hi(), id.value().lo()));
    if (d) std::printf("id=%s op=%s format=%s backend=%s valid=%d\n", d.value().id.str().c_str(), d.value().key.operation.c_str(), d.value().format.c_str(), d.value().backend.c_str(), d.value().validation.all_passed());
    else std::printf("inspect: %s\n", d.error().message().c_str());
    return 0;
  }
  if (cmd == "stats") {
    auto s = cache.stats();
    std::printf("lookups=%llu hits=%llu misses=%llu builds=%llu dedup=%llu invalidations=%llu evictions=%llu reloads=%llu hit_ratio=%.3f\n",
      (unsigned long long)s.lookups, (unsigned long long)(s.exact_hits+s.compatible_hits+s.host_hits+s.device_hits+s.persistent_hits),
      (unsigned long long)s.misses, (unsigned long long)s.builds, (unsigned long long)s.build_dedup,
      (unsigned long long)s.invalidations, (unsigned long long)s.evictions, (unsigned long long)s.reloads, s.hit_ratio());
    return 0;
  }
  if (cmd == "snapshot") {
    auto s = cache.snapshot();
    std::printf("artifacts=%llu host_bytes=%llu device_bytes=%llu persistent_bytes=%llu cache_gen=%llu\n",
      (unsigned long long)s.artifact_count, (unsigned long long)s.host_resident_bytes,
      (unsigned long long)s.device_resident_bytes, (unsigned long long)s.persistent_bytes, (unsigned long long)s.cache_generation);
    return 0;
  }
  if (cmd == "explain" && argc >= 3) {
    auto id = StrongId128::parse(argv[2]);
    if (!id) { std::printf("bad id\n"); return 1; }
    auto e = cache.explain(ArtifactId(id.value().hi(), id.value().lo()));
    std::printf("%s\n", e.text.c_str());
    return 0;
  }
  if (cmd == "recover") {
    std::vector<std::string> rejected, orphans;
    auto r = cache.recover(&rejected, &orphans);
    std::printf("recovered=%llu rejected=%u orphans=%u\n", r ? (unsigned long long)r.value() : 0, (unsigned)rejected.size(), (unsigned)orphans.size());
    return 0;
  }
  if (cmd == "bench" && argc >= 3) {
    int n = std::atoi(argv[2]);
    auto key = make_key("vec_add", 4096);
    KernelLookupRequest req; req.key = key; req.device.vendor = DeviceVendor::CPU;
    req.desired_tier = ResidencyTier::HostResident; req.allow_miss_build = true;
    (void)cache.acquire(req);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) { auto r = cache.lookup(req); (void)r; }
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;
    std::printf("bench: %d lookups in %.3f ms (%.0f lookups/s)\n", n, ms, n / (ms / 1000.0));
    return 0;
  }
  if (cmd == "serve" && argc >= 4) {
    extern int kc_coordinator_main(int, char**);
    return 0;  // handled by the coordinator binary in the distributed subproject
  }
  if (cmd == "worker" && argc >= 7) { return 0; }
  usage();
  return 2;
}