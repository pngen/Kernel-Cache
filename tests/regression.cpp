// regression.cpp - regression scenarios for single-flight publication, eviction,
// unload/load gating, validation, promotion races, budgets, invalidation and
// key identity. A probe backend holds its builder/validator/loader calls at
// gates and counts them, so each interleaving is forced instead of hoped for.
// Pass a scenario name to run only that scenario.
#include "kernelcache/kernelcache.hpp"
#include "test_util.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace kernelcache;

namespace {

std::string tmp_root(const char* tag) {
  return std::filesystem::temp_directory_path().string() + "/kc_reg_" + tag + "_" + std::to_string(kc_pid());
}

// Shared probe state. Every field is guarded by mu; waits use cv.
struct Probe {
  std::mutex mu;
  std::condition_variable cv;
  bool hold_build = false, hold_validate = false, hold_load = false, hold_unload = false;
  bool throw_build = false;     // the next build throws
  bool fail_validate = false;   // validations report failure
  int load_rendezvous = 0;      // loads wait until this many have entered
  int builds_entered = 0, validates_entered = 0, loads_entered = 0, unloads_entered = 0;
  std::size_t validate_size_at_entry = 0, validate_size_at_exit = 0;
  int next_module = 1;
  std::set<int> live_modules;             // loaded and not yet unloaded
  std::map<int, int> unloads_per_module;
  std::uint64_t footprint = 1000;         // set before the cache is used

  void wait_until(const std::function<bool()>& pred) {
    std::unique_lock<std::mutex> lk(mu);
    cv.wait(lk, pred);
  }
  void set(const std::function<void()>& f) {
    { std::lock_guard<std::mutex> lk(mu); f(); }
    cv.notify_all();
  }
};

struct ProbeModule { int id = 0; };

class ProbeBuilder : public KernelBuilder {
 public:
  explicit ProbeBuilder(Probe& p) : p_(p) {}
  std::string backend_name() const override { return "probe"; }
  bool can_build(const KernelCompatibilityKey& key) const override { return key.runtime_backend == "probe"; }
  Result<BuildResult> build(const BuildRequest& req) override {
    bool do_throw = false;
    {
      std::unique_lock<std::mutex> lk(p_.mu);
      ++p_.builds_entered;
      p_.cv.notify_all();
      p_.cv.wait(lk, [&] { return !p_.hold_build; });
      do_throw = p_.throw_build;
      p_.throw_build = false;
    }
    if (do_throw) throw std::runtime_error("probe builder failure");
    BuildResult br;
    br.artifact_format = "probe";
    std::string body = "probe:" + req.key.operation + ":" + req.key.digest_hex();
    br.bytes.assign(body.begin(), body.end());
    br.sizes.artifact_bytes = br.bytes.size();
    br.sizes.host_bytes = br.bytes.size();
    br.sizes.storage_bytes = br.bytes.size();
    br.provenance.producer = "probe";
    br.kernel_count = 1;
    return br;
  }
 private:
  Probe& p_;
};

class ProbeValidator : public KernelValidator {
 public:
  explicit ProbeValidator(Probe& p) : p_(p) {}
  std::string backend_name() const override { return "probe"; }
  Result<ValidationDescriptor> validate(const KernelCompatibilityKey&, const std::vector<std::uint8_t>& bytes,
                                        const DeviceDescriptor&) override {
    bool fail = false;
    {
      std::unique_lock<std::mutex> lk(p_.mu);
      ++p_.validates_entered;
      p_.validate_size_at_entry = bytes.size();
      p_.cv.notify_all();
      p_.cv.wait(lk, [&] { return !p_.hold_validate; });
      p_.validate_size_at_exit = bytes.size();
      fail = p_.fail_validate;
    }
    const bool ok = !fail && !bytes.empty();
    ValidationDescriptor vd;
    vd.integrity_hash_checked = ok; vd.format_checked = ok; vd.arch_checked = ok; vd.abi_checked = ok;
    vd.metadata_consistent = ok; vd.loadable = ok; vd.execution_smoke = ok; vd.reference_compare = ok;
    if (!ok) vd.backend_reason = "probe validation failure";
    return vd;
  }
 private:
  Probe& p_;
};

class ProbeLoader : public KernelLoader {
 public:
  explicit ProbeLoader(Probe& p) : p_(p) {}
  std::string backend_name() const override { return "probe"; }
  Result<LoadedModuleHandle> load(const KernelCompatibilityKey&, const std::vector<std::uint8_t>&,
                                  const DeviceDescriptor&) override {
    auto m = std::make_shared<ProbeModule>();
    std::unique_lock<std::mutex> lk(p_.mu);
    ++p_.loads_entered;
    p_.cv.notify_all();
    p_.cv.wait(lk, [&] { return !p_.hold_load && p_.loads_entered >= p_.load_rendezvous; });
    m->id = p_.next_module++;
    p_.live_modules.insert(m->id);
    return LoadedModuleHandle(std::static_pointer_cast<void>(m));
  }
  Result<void> unload(LoadedModuleHandle handle) override {
    if (!handle) return Result<void>();
    auto m = std::static_pointer_cast<ProbeModule>(handle);
    std::unique_lock<std::mutex> lk(p_.mu);
    ++p_.unloads_entered;
    p_.cv.notify_all();
    p_.cv.wait(lk, [&] { return !p_.hold_unload; });
    ++p_.unloads_per_module[m->id];
    p_.live_modules.erase(m->id);
    return Result<void>();
  }
  Result<void> execute(LoadedModuleHandle handle, void*, std::size_t) override {
    if (!handle) return Result<void>(ErrorCode::InvalidArgument, "null handle");
    auto m = std::static_pointer_cast<ProbeModule>(handle);
    std::lock_guard<std::mutex> lk(p_.mu);
    if (!p_.live_modules.count(m->id)) return Result<void>(ErrorCode::InvalidArgument, "module was unloaded");
    return Result<void>();
  }
  std::pair<std::uint64_t, bool> resident_footprint(const LoadedModuleHandle&) const override {
    return {p_.footprint, true};
  }
 private:
  Probe& p_;
};

KernelCacheConfig no_persistence() {
  KernelCacheConfig cfg;
  cfg.persistence_enabled = false;
  return cfg;
}

// A cache whose only backend is the probe. Members are destroyed in reverse
// order, so the cache goes before the backend objects it points to.
struct Rig {
  Probe probe;
  ProbeBuilder builder{probe};
  ProbeValidator validator{probe};
  ProbeLoader loader{probe};
  std::unique_ptr<KernelCache> cache;
  explicit Rig(KernelCacheConfig cfg = no_persistence()) {
    cache = std::make_unique<KernelCache>(std::move(cfg));
    KernelBackend be;
    be.name = "probe"; be.builder = &builder; be.validator = &validator; be.loader = &loader;
    CHECK_TRUE(cache->register_backend(be).ok());
  }
};

KernelCompatibilityKey probe_key(const std::string& op) {
  KernelCompatibilityKey k;
  k.operation = op; k.artifact_format = "probe";
  k.vendor = DeviceVendor::CPU; k.family = AcceleratorFamily::CPU; k.arch = "x86_64";
  k.runtime_backend = "probe"; k.compiler_name = "probe"; k.compiler_version_major = 1;
  k.abi_name = "probe-abi"; k.abi_version = 1; k.kernel_interface_signature = "void(void*)";
  k.dtypes = {Datatype::F32}; k.layouts = {TensorLayout::RowMajor}; k.rank = 1; k.shape = {256};
  k.alignment_bytes = 16; k.block_x = 128; k.launch_abi = "grid-block";
  k.finalize();
  return k;
}

KernelLookupRequest probe_req(const std::string& op, ResidencyTier tier, bool allow_build) {
  KernelLookupRequest r;
  r.key = probe_key(op);
  r.device.vendor = DeviceVendor::CPU; r.device.family = AcceleratorFamily::CPU;
  r.desired_tier = tier;
  r.allow_miss_build = allow_build;
  return r;
}

ValidationDescriptor passed_validation() {
  ValidationDescriptor vd;
  vd.integrity_hash_checked = vd.format_checked = vd.arch_checked = vd.abi_checked = true;
  vd.metadata_consistent = vd.loadable = vd.execution_smoke = vd.reference_compare = true;
  return vd;
}

bool in_state(KernelCache& c, const ArtifactId& id, ArtifactState s) {
  auto l = c.list(std::nullopt, s);
  if (!l.ok()) return false;
  for (auto& d : l.value()) if (d.id == id) return true;
  return false;
}

InvalidationRequest by_id(const ArtifactId& id) {
  return InvalidationRequest{InvalidationTarget::ArtifactId, id.lo(), id.hi()};
}

struct Out { bool ok = false; ArtifactId id; std::string err; };

// Acquire, record the artifact id, release.
ArtifactId acquire_release(KernelCache& c, const KernelLookupRequest& req) {
  auto l = c.acquire(req);
  if (!l.ok()) std::cerr << "acquire: " << l.error().message() << "\n";
  CHECK_TRUE(l.ok());
  ArtifactId id = l.value().artifact_id();
  l.value().release();
  return id;
}

// ---------------------------------------------------------------------------
// A waiter released by the single-flight fence must find the built artifact.
// On Windows the process is confined to one CPU with the owner below the
// waiters, so the waiters run as soon as the owner resolves the fence.
void scenario_build_publish() {
  Rig rig;
  auto req = probe_req("sf_publish", ResidencyTier::HostResident, true);
  const int kWaiters = 4;
#ifdef _WIN32
  DWORD_PTR proc_mask = 0, sys_mask = 0;
  CHECK_TRUE(GetProcessAffinityMask(GetCurrentProcess(), &proc_mask, &sys_mask) != 0);
  const DWORD_PTR one_cpu = proc_mask & (~proc_mask + 1);
  CHECK_TRUE(SetProcessAffinityMask(GetCurrentProcess(), one_cpu) != 0);
#endif
  rig.probe.set([&] { rig.probe.hold_build = true; });
  Out owner_out;
  std::vector<Out> waiter_out(kWaiters);
  std::thread owner([&] {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#endif
    auto r = rig.cache->build(req);
    owner_out.ok = r.ok();
    if (r.ok()) owner_out.id = r.value().id; else owner_out.err = r.error().message();
  });
  rig.probe.wait_until([&] { return rig.probe.builds_entered == 1; });
  std::vector<std::thread> waiters;
  for (int i = 0; i < kWaiters; ++i) {
    waiters.emplace_back([&, i] {
#ifdef _WIN32
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
      auto r = rig.cache->build(req);
      waiter_out[i].ok = r.ok();
      if (r.ok()) waiter_out[i].id = r.value().id; else waiter_out[i].err = r.error().message();
    });
  }
  while (rig.cache->stats().build_dedup < static_cast<std::uint64_t>(kWaiters))
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  rig.probe.set([&] { rig.probe.hold_build = false; });
  owner.join();
  for (auto& t : waiters) t.join();
#ifdef _WIN32
  CHECK_TRUE(SetProcessAffinityMask(GetCurrentProcess(), proc_mask) != 0);
#endif
  if (!owner_out.ok) std::cerr << "owner: " << owner_out.err << "\n";
  CHECK_TRUE(owner_out.ok);
  for (auto& w : waiter_out) {
    if (!w.ok) std::cerr << "waiter: " << w.err << "\n";
    CHECK_TRUE(w.ok);
    CHECK_EQ(w.id, owner_out.id);
  }
  CHECK_EQ(rig.probe.builds_entered, 1);
}

// A throwing builder resolves the fence: the owner and the waiter get an error
// and the key can be built again.
void scenario_build_exception() {
  Rig rig;
  auto req = probe_req("sf_throw", ResidencyTier::HostResident, true);
  rig.probe.set([&] { rig.probe.hold_build = true; rig.probe.throw_build = true; });
  std::atomic<bool> owner_threw{false};
  Out owner_out, waiter_out;
  std::thread owner([&] {
    try {
      auto r = rig.cache->build(req);
      owner_out.ok = r.ok();
    } catch (const std::exception&) {
      owner_threw = true;
    }
  });
  rig.probe.wait_until([&] { return rig.probe.builds_entered == 1; });
  std::thread waiter([&] {
    auto r = rig.cache->build(req);
    waiter_out.ok = r.ok();
  });
  while (rig.cache->stats().build_dedup < 1) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  rig.probe.set([&] { rig.probe.hold_build = false; });
  owner.join();
  CHECK_FALSE(owner_threw.load());
  waiter.join();
  CHECK_FALSE(owner_out.ok);
  CHECK_FALSE(waiter_out.ok);
  auto again = rig.cache->build(req);
  CHECK_TRUE(again.ok());
  CHECK_EQ(rig.cache->stats().active_builds, 0u);
}

// Persisting a freshly built artifact never demotes the residency that released
// waiters already established (same one-CPU/priority setup as build_publish).
void scenario_persist_keeps_residency() {
  std::string root = tmp_root("persisttier");
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  KernelCacheConfig cfg;
  cfg.persistence_root = root;
  cfg.persistence_enabled = true;
  {
    Rig rig(cfg);
    auto req = probe_req("persist_tier", ResidencyTier::HostResident, true);
    const int kWaiters = 3;
#ifdef _WIN32
    DWORD_PTR proc_mask = 0, sys_mask = 0;
    CHECK_TRUE(GetProcessAffinityMask(GetCurrentProcess(), &proc_mask, &sys_mask) != 0);
    const DWORD_PTR one_cpu = proc_mask & (~proc_mask + 1);
    CHECK_TRUE(SetProcessAffinityMask(GetCurrentProcess(), one_cpu) != 0);
#endif
    rig.probe.set([&] { rig.probe.hold_build = true; });
    bool owner_ok = false;
    std::thread owner([&] {
#ifdef _WIN32
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#endif
      owner_ok = rig.cache->build(req).ok();
    });
    rig.probe.wait_until([&] { return rig.probe.builds_entered == 1; });
    std::vector<KernelLease> leases(kWaiters);
    std::vector<int> acquired(kWaiters, 0);
    std::vector<std::thread> waiters;
    for (int i = 0; i < kWaiters; ++i) {
      waiters.emplace_back([&, i] {
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
        auto l = rig.cache->acquire(req);
        if (l.ok()) { acquired[i] = 1; leases[i] = std::move(l.value()); }
      });
    }
    while (rig.cache->stats().build_dedup < static_cast<std::uint64_t>(kWaiters))
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    rig.probe.set([&] { rig.probe.hold_build = false; });
    owner.join();
    for (auto& t : waiters) t.join();
#ifdef _WIN32
    CHECK_TRUE(SetProcessAffinityMask(GetCurrentProcess(), proc_mask) != 0);
#endif
    CHECK_TRUE(owner_ok);
    for (int i = 0; i < kWaiters; ++i) {
      CHECK_EQ(acquired[i], 1);
      CHECK_EQ(leases[i].residency(), ResidencyTier::HostResident);
      leases[i].release();
    }
  }
  std::filesystem::remove_all(root, ec);
}

// A failed persistence write is reported and leaves the cache usable.
void scenario_persist_failure() {
  std::string root = tmp_root("persistfail");
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  KernelCacheConfig cfg;
  cfg.persistence_root = root;
  cfg.persistence_enabled = true;
  {
    Rig rig(cfg);
    std::filesystem::remove_all(root + "/tmp", ec);
    { std::ofstream f(root + "/tmp"); f << "not a directory"; }
    auto req = probe_req("persist_fail", ResidencyTier::HostResident, true);
    auto l = rig.cache->acquire(req);
    CHECK_TRUE(l.ok());
    bool reported = false;
    for (auto& e : rig.cache->events().recent(4096))
      if (e.type == "persist" && e.message.rfind("failed", 0) == 0) reported = true;
    CHECK_TRUE(reported);
    l.value().release();
    auto snap = rig.cache->snapshot();
    CHECK_EQ(snap.artifact_count, 1u);
    CHECK_EQ(rig.cache->stats().active_leases, 0u);
  }
  std::filesystem::remove_all(root, ec);
}

// While an eviction unloads the module, the artifact cannot be leased.
void scenario_evict_blocks_leases() {
  Rig rig;
  auto req = probe_req("evict_lease", ResidencyTier::DeviceResident, true);
  ArtifactId id = acquire_release(*rig.cache, req);
  rig.probe.set([&] { rig.probe.hold_unload = true; });
  Out ev;
  std::thread evictor([&] {
    auto r = rig.cache->evict(id, false);
    ev.ok = r.ok();
    if (!r.ok()) ev.err = r.error().message();
  });
  rig.probe.wait_until([&] { return rig.probe.unloads_entered == 1; });
  auto no_build = req;
  no_build.allow_miss_build = false;
  auto during = rig.cache->acquire(no_build);
  const bool leased_during = during.ok();
  if (during.ok()) during.value().release();
  rig.probe.set([&] { rig.probe.hold_unload = false; });
  evictor.join();
  CHECK_FALSE(leased_during);
  if (!ev.ok) std::cerr << "evict: " << ev.err << "\n";
  CHECK_TRUE(ev.ok);
}

// Two concurrent evictions unload the module once.
void scenario_single_unload() {
  Rig rig;
  auto req = probe_req("double_unload", ResidencyTier::DeviceResident, true);
  ArtifactId id = acquire_release(*rig.cache, req);
  rig.probe.set([&] { rig.probe.hold_unload = true; });
  std::thread e1([&] { (void)rig.cache->evict(id, false); });
  rig.probe.wait_until([&] { return rig.probe.unloads_entered == 1; });
  bool e2_done = false;
  std::thread e2([&] {
    (void)rig.cache->evict(id, false);
    rig.probe.set([&] { e2_done = true; });
  });
  rig.probe.wait_until([&] { return e2_done || rig.probe.unloads_entered >= 2; });
  rig.probe.set([&] { rig.probe.hold_unload = false; });
  e1.join();
  e2.join();
  CHECK_EQ(rig.probe.unloads_entered, 1);
  for (auto& entry : rig.probe.unloads_per_module) CHECK_EQ(entry.second, 1);
}

// An evicted artifact is reloaded, not rebuilt.
void scenario_evict_reload() {
  {
    // Never leased: built, loaded to the device directly, then evicted.
    Rig rig;
    auto req = probe_req("reload_built", ResidencyTier::DeviceResident, true);
    auto b = rig.cache->build(req);
    CHECK_TRUE(b.ok());
    ArtifactId id = b.value().id;
    CHECK_TRUE(rig.cache->load(id).ok());
    CHECK_TRUE(rig.cache->evict(id, false).ok());
    auto look = rig.cache->lookup(req);
    CHECK_TRUE(look.ok());
    CHECK_TRUE(look.value().is_hit());
    CHECK_TRUE(look.value().artifact.has_value() && *look.value().artifact == id);
    auto no_build = req;
    no_build.allow_miss_build = false;
    auto l = rig.cache->acquire(no_build);
    CHECK_TRUE(l.ok());
    CHECK_EQ(l.value().artifact_id(), id);
    l.value().release();
    CHECK_EQ(rig.probe.builds_entered, 1);
  }
  {
    Rig rig;
    auto req = probe_req("reload_device", ResidencyTier::DeviceResident, true);
    ArtifactId id = acquire_release(*rig.cache, req);
    CHECK_TRUE(rig.cache->evict(id, false).ok());
    auto look = rig.cache->lookup(req);
    CHECK_TRUE(look.ok());
    CHECK_TRUE(look.value().is_hit());
    CHECK_TRUE(look.value().artifact.has_value() && *look.value().artifact == id);
    auto no_build = req;
    no_build.allow_miss_build = false;
    auto l = rig.cache->acquire(no_build);
    CHECK_TRUE(l.ok());
    CHECK_EQ(l.value().artifact_id(), id);
    CHECK_TRUE(l.value().native_handle() != nullptr);
    auto h = rig.cache->load(id);
    CHECK_TRUE(h.ok());
    CHECK_TRUE(rig.cache->execute(h.value(), nullptr, 0).ok());
    l.value().release();
    CHECK_EQ(rig.probe.builds_entered, 1);
  }
  {
    std::string root = tmp_root("reload");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    KernelCacheConfig cfg;
    cfg.persistence_root = root;
    cfg.persistence_enabled = true;
    {
      Rig rig(cfg);
      auto req = probe_req("reload_host", ResidencyTier::HostResident, true);
      ArtifactId id = acquire_release(*rig.cache, req);
      CHECK_TRUE(rig.cache->evict(id, false).ok());
      auto no_build = req;
      no_build.allow_miss_build = false;
      auto l = rig.cache->acquire(no_build);
      CHECK_TRUE(l.ok());
      CHECK_EQ(l.value().artifact_id(), id);
      l.value().release();
      CHECK_EQ(rig.probe.builds_entered, 1);
    }
    std::filesystem::remove_all(root, ec);
  }
}

// unload() never unloads a leased artifact's module, and leaves it reloadable.
void scenario_unload_respects_leases() {
  Rig rig;
  auto req = probe_req("unload_leased", ResidencyTier::DeviceResident, true);
  auto l = rig.cache->acquire(req);
  CHECK_TRUE(l.ok());
  ArtifactId id = l.value().artifact_id();
  auto h = rig.cache->load(id);
  CHECK_TRUE(h.ok());
  CHECK_FALSE(rig.cache->unload(id).ok());
  CHECK_EQ(rig.probe.unloads_entered, 0);
  CHECK_TRUE(rig.cache->execute(h.value(), nullptr, 0).ok());
  l.value().release();
  CHECK_TRUE(rig.cache->unload(id).ok());
  CHECK_EQ(rig.probe.unloads_entered, 1);
  auto no_build = req;
  no_build.allow_miss_build = false;
  auto l2 = rig.cache->acquire(no_build);
  CHECK_TRUE(l2.ok());
  CHECK_EQ(l2.value().artifact_id(), id);
  l2.value().release();
}

// load() refuses invalidated and unvalidated artifacts.
void scenario_load_gate() {
  Rig rig;
  auto req = probe_req("load_gate", ResidencyTier::HostResident, true);
  ArtifactId id = acquire_release(*rig.cache, req);
  CHECK_TRUE(rig.cache->invalidate(by_id(id)).ok());
  const int loads_before = rig.probe.loads_entered;
  CHECK_FALSE(rig.cache->load(id).ok());
  ArtifactDescriptor d;
  d.key = probe_key("load_gate_unvalidated");
  d.format = "probe";
  d.generation.value = 1;
  auto pid = rig.cache->put(d, std::vector<std::uint8_t>{1, 2, 3, 4});
  CHECK_TRUE(pid.ok());
  CHECK_FALSE(rig.cache->load(pid.value()).ok());
  CHECK_EQ(rig.probe.loads_entered, loads_before);
}

// validate() works on bytes an eviction cannot free underneath it.
void scenario_validate_stable_bytes() {
  Rig rig;
  auto req = probe_req("validate_evict", ResidencyTier::HostResident, true);
  ArtifactId id = acquire_release(*rig.cache, req);
  const int base = rig.probe.validates_entered;
  rig.probe.set([&] { rig.probe.hold_validate = true; });
  std::thread v([&] { (void)rig.cache->validate(req, id); });
  rig.probe.wait_until([&] { return rig.probe.validates_entered == base + 1; });
  CHECK_TRUE(rig.cache->evict(id, false).ok());
  rig.probe.set([&] { rig.probe.hold_validate = false; });
  v.join();
  CHECK_TRUE(rig.probe.validate_size_at_entry > 0);
  CHECK_EQ(rig.probe.validate_size_at_exit, rig.probe.validate_size_at_entry);
}

// A failed re-validation of a leased artifact retires it when the lease drains.
void scenario_failed_validation_retires() {
  Rig rig;
  auto req = probe_req("revalidate", ResidencyTier::HostResident, true);
  auto l = rig.cache->acquire(req);
  CHECK_TRUE(l.ok());
  ArtifactId id = l.value().artifact_id();
  rig.probe.set([&] { rig.probe.fail_validate = true; });
  auto vr = rig.cache->validate(req, id);
  CHECK_TRUE(vr.ok());
  CHECK_FALSE(vr.value().all_passed());
  l.value().release();
  CHECK_TRUE(in_state(*rig.cache, id, ArtifactState::Retired));
  auto no_build = req;
  no_build.allow_miss_build = false;
  CHECK_FALSE(rig.cache->acquire(no_build).ok());
}

// Two concurrent promotions to device leave exactly one module loaded.
void scenario_concurrent_promotion() {
  Rig rig;
  ArtifactId id = acquire_release(*rig.cache, probe_req("promote_race", ResidencyTier::HostResident, true));
  rig.probe.set([&] { rig.probe.load_rendezvous = 2; });
  auto dev = probe_req("promote_race", ResidencyTier::DeviceResident, false);
  Out a, b;
  auto run = [&](Out& o) {
    auto l = rig.cache->acquire(dev);
    o.ok = l.ok();
    if (l.ok()) { o.id = l.value().artifact_id(); l.value().release(); }
  };
  std::thread ta([&] { run(a); });
  std::thread tb([&] { run(b); });
  ta.join();
  tb.join();
  rig.probe.set([&] { rig.probe.load_rendezvous = 0; });
  CHECK_TRUE(a.ok);
  CHECK_TRUE(b.ok);
  CHECK_EQ(a.id, id);
  CHECK_EQ(b.id, id);
  CHECK_EQ(rig.probe.loads_entered, 2);
  CHECK_TRUE(rig.cache->evict(id, false).ok());
  CHECK_TRUE(rig.probe.live_modules.empty());
}

// A module that finished loading after its artifact was invalidated is not
// installed; it is unloaded.
void scenario_promotion_after_invalidation() {
  Rig rig;
  ArtifactId id = acquire_release(*rig.cache, probe_req("promote_inval", ResidencyTier::HostResident, true));
  rig.probe.set([&] { rig.probe.hold_load = true; });
  Out a;
  std::thread ta([&] {
    auto l = rig.cache->acquire(probe_req("promote_inval", ResidencyTier::DeviceResident, false));
    a.ok = l.ok();
    if (l.ok()) l.value().release();
  });
  rig.probe.wait_until([&] { return rig.probe.loads_entered == 1; });
  CHECK_TRUE(rig.cache->invalidate(by_id(id)).ok());
  rig.probe.set([&] { rig.probe.hold_load = false; });
  ta.join();
  CHECK_FALSE(a.ok);
  CHECK_TRUE(rig.probe.live_modules.empty());
}

// Pinned and leased residents count against the device budget.
void scenario_budget_counts_all_residents() {
  KernelCacheConfig cfg = no_persistence();
  cfg.residency.device_budget_bytes = 2500;   // the probe reports 1000 bytes per module
  Rig rig(cfg);
  auto la = rig.cache->acquire(probe_req("budget_a", ResidencyTier::DeviceResident, true));
  CHECK_TRUE(la.ok());
  ArtifactId ida = la.value().artifact_id();
  CHECK_TRUE(rig.cache->pin(ida).ok());
  la.value().release();
  (void)acquire_release(*rig.cache, probe_req("budget_b", ResidencyTier::DeviceResident, true));
  auto host_c = rig.cache->acquire(probe_req("budget_c", ResidencyTier::HostResident, true));
  CHECK_TRUE(host_c.ok());
  auto dev_c = rig.cache->acquire(probe_req("budget_c", ResidencyTier::DeviceResident, false));
  CHECK_TRUE(dev_c.ok());
  auto snap = rig.cache->snapshot();
  CHECK_TRUE(snap.device_resident_bytes <= cfg.residency.device_budget_bytes);
  dev_c.value().release();
  host_c.value().release();
  CHECK_TRUE(rig.cache->unpin(ida).ok());
}

// Invalidation retires loaded device state: at once without leases, otherwise
// when the last lease drains.
void scenario_invalidation_retires_modules() {
  Rig rig;
  ArtifactId id = acquire_release(*rig.cache, probe_req("inval_unleased", ResidencyTier::DeviceResident, true));
  CHECK_EQ(rig.probe.live_modules.size(), 1u);
  CHECK_TRUE(rig.cache->invalidate(by_id(id)).ok());
  CHECK_TRUE(rig.probe.live_modules.empty());
  auto l = rig.cache->acquire(probe_req("inval_leased", ResidencyTier::DeviceResident, true));
  CHECK_TRUE(l.ok());
  ArtifactId id2 = l.value().artifact_id();
  CHECK_TRUE(rig.cache->invalidate(by_id(id2)).ok());
  CHECK_EQ(rig.probe.live_modules.size(), 1u);
  l.value().release();
  CHECK_TRUE(rig.probe.live_modules.empty());
  CHECK_TRUE(in_state(*rig.cache, id2, ArtifactState::Retired));
}

// A key that fails finalize() never impersonates the key it was copied from.
void scenario_invalid_key_identity() {
  Rig rig;
  KernelCompatibilityKey good = probe_key("identity");
  KernelCompatibilityKey bad = good;
  bad.layouts.push_back(TensorLayout::NCHW);   // dtypes/layouts mismatch
  CompatibilityPolicy pol;
  CHECK_FALSE(evaluate_compatibility(good, bad, pol).is_compatible());
  ArtifactDescriptor d;
  d.key = bad;
  d.format = "probe";
  d.generation.value = 1;
  d.validation = passed_validation();
  CHECK_FALSE(rig.cache->put(d, std::vector<std::uint8_t>{1, 2, 3}).ok());
  auto rb = probe_req("identity", ResidencyTier::HostResident, true);
  rb.key = bad;
  CHECK_FALSE(rig.cache->build(rb).ok());
  auto look = rig.cache->lookup(probe_req("identity", ResidencyTier::HostResident, false));
  CHECK_TRUE(look.ok());
  CHECK_FALSE(look.value().is_hit());
}

// A lease taken with pin=true unpins when released.
void scenario_lease_pin_released() {
  Rig rig;
  auto req = probe_req("lease_pin", ResidencyTier::DeviceResident, true);
  req.pin = true;
  ArtifactId id = acquire_release(*rig.cache, req);
  CHECK_TRUE(rig.cache->evict(id, false).ok());
}

}  // namespace

int main(int argc, char** argv) {
  struct Scenario { const char* name; void (*fn)(); };
  const Scenario all[] = {
    {"build_publish", scenario_build_publish},
    {"build_exception", scenario_build_exception},
    {"persist_keeps_residency", scenario_persist_keeps_residency},
    {"persist_failure", scenario_persist_failure},
    {"evict_blocks_leases", scenario_evict_blocks_leases},
    {"single_unload", scenario_single_unload},
    {"evict_reload", scenario_evict_reload},
    {"unload_respects_leases", scenario_unload_respects_leases},
    {"load_gate", scenario_load_gate},
    {"validate_stable_bytes", scenario_validate_stable_bytes},
    {"failed_validation_retires", scenario_failed_validation_retires},
    {"concurrent_promotion", scenario_concurrent_promotion},
    {"promotion_after_invalidation", scenario_promotion_after_invalidation},
    {"budget_counts_all_residents", scenario_budget_counts_all_residents},
    {"invalidation_retires_modules", scenario_invalidation_retires_modules},
    {"invalid_key_identity", scenario_invalid_key_identity},
    {"lease_pin_released", scenario_lease_pin_released},
  };
  int ran = 0;
  for (const auto& s : all) {
    if (argc > 1 && std::strcmp(argv[1], s.name) != 0) continue;
    s.fn();
    std::cout << "scenario " << s.name << " OK\n";
    ++ran;
  }
  if (ran == 0) { std::cerr << "unknown scenario\n"; return 2; }
  std::cout << "REGRESSION_OK scenarios=" << ran << " checks=" << kctest::g_checks << "\n";
  return 0;
}
