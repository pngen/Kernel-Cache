// dist_regression.cpp - coordinator regression scenarios over real processes:
// a restarted worker replaces its registration, epochs never move backwards, a
// malformed epoch roll is ignored, and concurrent clients plus epoch rolls keep
// every build consistent. Pass a scenario name to run only that scenario.
#include "kernelcache/kernelcache.hpp"
#include "kernelcache/distributed.hpp"
#include "test_util.hpp"
#include "dist_tcp.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace kernelcache;

#ifdef _WIN32
namespace {

std::string dist_dir() {
#ifdef KC_DIST_DIR
  return KC_DIST_DIR;
#else
  return ".";
#endif
}

std::string tmp_root(const char* tag) {
  return std::filesystem::temp_directory_path().string() + "/kc_distreg_" + tag + "_" + std::to_string(kc_pid());
}

std::uint16_t pick_port() {
  static bool seeded = [] {
    std::srand(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return true;
  }();
  (void)seeded;
  return static_cast<std::uint16_t>(43000 + (std::rand() % 8000));
}

HANDLE spawn(const std::string& exe, const std::vector<std::string>& args) {
  std::string cmdline = "\"" + exe + "\"";
  for (auto& a : args) cmdline += " \"" + a + "\"";
  std::wstring wcmd(cmdline.begin(), cmdline.end());
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(std::wstring(exe.begin(), exe.end()).c_str(), &wcmd[0], nullptr, nullptr, FALSE, 0,
                      nullptr, nullptr, &si, &pi))
    return nullptr;
  CloseHandle(pi.hThread);
  return pi.hProcess;
}

bool exited(HANDLE h) { return WaitForSingleObject(h, 0) == WAIT_OBJECT_0; }

void kill_wait(HANDLE h) {
  if (!h) return;
  TerminateProcess(h, 0);
  WaitForSingleObject(h, INFINITE);
  CloseHandle(h);
}

// A coordinator plus its workers; every process is terminated on scope exit.
struct Cluster {
  HANDLE coord = nullptr;
  std::vector<HANDLE> workers;
  std::uint16_t port = 0;
  std::string root;
  ~Cluster() {
    for (auto h : workers) kill_wait(h);
    kill_wait(coord);
    std::error_code ec;
    if (!root.empty()) std::filesystem::remove_all(root, ec);
  }
};

void start_coordinator(Cluster& cl, const char* tag) {
  cl.root = tmp_root(tag);
  std::error_code ec;
  std::filesystem::remove_all(cl.root, ec);
  for (;;) {
    cl.port = pick_port();
    cl.coord = spawn(dist_dir() + "/kc_coordinator.exe", {std::to_string(cl.port), cl.root});
    CHECK_TRUE(cl.coord != nullptr);
    for (;;) {
      if (exited(cl.coord)) break;   // bind failed: retry on another port
      dist::TcpConnection probe;
      const bool up = probe.connect("127.0.0.1", cl.port);
      probe.close();
      if (up) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CloseHandle(cl.coord);
    cl.coord = nullptr;
  }
}

HANDLE start_worker(Cluster& cl, std::uint64_t id, std::uint64_t boot_hi, std::uint64_t boot_lo) {
  HANDLE h = spawn(dist_dir() + "/kc_worker.exe",
                   {"127.0.0.1", std::to_string(cl.port), std::to_string(id), std::to_string(boot_hi),
                    std::to_string(boot_lo)});
  CHECK_TRUE(h != nullptr);
  cl.workers.push_back(h);
  return h;
}

std::vector<std::uint64_t> query_workers(dist::TcpConnection& conn) {
  if (!conn.write_frame(static_cast<std::uint16_t>(DistMsgType::QueryWorkers), encode_query_workers())) return {};
  std::uint16_t t = 0;
  std::vector<std::uint8_t> p;
  if (!conn.read_frame(t, p) || t != static_cast<std::uint16_t>(DistMsgType::QueryWorkersResp)) return {};
  auto boots = decode_query_workers_resp(p);
  return boots ? boots.value() : std::vector<std::uint64_t>{};
}

bool listed(const std::vector<std::uint64_t>& boots, std::uint64_t boot_hi) {
  for (auto b : boots) if (b == boot_hi) return true;
  return false;
}

void wait_registered(dist::TcpConnection& conn, HANDLE worker, std::uint64_t boot_hi) {
  while (!listed(query_workers(conn), boot_hi)) {
    if (exited(worker)) kctest::fail("worker exited before registering", __LINE__);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

DistAuthority make_auth(std::uint64_t epoch, std::uint64_t gen, std::uint64_t boot_hi, std::uint64_t boot_lo) {
  DistAuthority a;
  a.epoch.value = epoch; a.worker.value = 1; a.boot = WorkerBootId{StrongId128(boot_hi, boot_lo)};
  a.cache_gen.value = gen; a.artifact_gen.value = 0; a.attempt = BuildAttemptId{}; a.request = RequestId{};
  return a;
}

KernelCompatibilityKey make_key(const std::string& op, std::int64_t shape) {
  KernelCompatibilityKey k;
  k.operation = op; k.artifact_format = "kc-synth";
  k.vendor = DeviceVendor::CPU; k.family = AcceleratorFamily::CPU; k.arch = "x86_64";
  k.runtime_backend = "cpu-synth"; k.compiler_name = "msvc"; k.compiler_version_major = 19; k.compiler_version_minor = 44;
  k.abi_name = "kc-synth-1.0"; k.abi_version = 1; k.kernel_interface_signature = "void(float*,const float*,uint64)";
  k.dtypes = {Datatype::F32}; k.layouts = {TensorLayout::RowMajor}; k.rank = 1; k.shape = {shape};
  k.alignment_bytes = 16; k.block_x = 256; k.launch_abi = "grid-block"; k.finalize();
  return k;
}

std::uint8_t lookup(dist::TcpConnection& conn, const DistAuthority& auth, RequestId rid, const KernelCompatibilityKey& k) {
  auto req = encode_lookup_request(auth, rid, k.operation,
      std::vector<std::uint8_t>(k.canonical_bytes().begin(), k.canonical_bytes().end()), "", 3);
  if (!conn.write_frame(static_cast<std::uint16_t>(DistMsgType::LookupReq), req)) return 0xFF;
  std::uint16_t t = 0;
  std::vector<std::uint8_t> pl;
  if (!conn.read_frame(t, pl) || t != static_cast<std::uint16_t>(DistMsgType::LookupResp)) return 0xFF;
  RequestId rr; std::uint8_t outcome = 0xFF; ArtifactId id; ArtifactGeneration gen; std::uint8_t compat = 0;
  if (!decode_lookup_response(pl, rr, outcome, id, gen, compat).ok()) return 0xFF;
  return outcome;
}

void roll(dist::TcpConnection& conn, std::uint64_t epoch) {
  (void)conn.write_frame(static_cast<std::uint16_t>(DistMsgType::RollEpoch), encode_roll_epoch(CoordinatorEpoch(epoch)));
}

// A restarted worker (same WorkerId, new boot) replaces its old registration.
void scenario_worker_restart() {
  Cluster cl;
  start_coordinator(cl, "restart");
  HANDLE w1 = start_worker(cl, 1, 0x1111, 1);
  HANDLE w2 = start_worker(cl, 2, 0x2222, 2);
  dist::TcpConnection conn;
  CHECK_TRUE(conn.connect("127.0.0.1", cl.port));
  wait_registered(conn, w1, 0x1111);
  wait_registered(conn, w2, 0x2222);
  TerminateProcess(w1, 0);
  WaitForSingleObject(w1, INFINITE);
  HANDLE w1b = start_worker(cl, 1, 0x3333, 3);
  wait_registered(conn, w1b, 0x3333);
  auto boots = query_workers(conn);
  CHECK_FALSE(listed(boots, 0x1111));
  CHECK_TRUE(listed(boots, 0x2222));
  auto auth = make_auth(1, 1, 0x3333, 3);
  auto key = make_key("restart_op", 2048);
  CHECK_EQ(lookup(conn, auth, RequestId(7, 1), key), 0x0B);
  CHECK_EQ(lookup(conn, auth, RequestId(7, 2), key), 0x00);
  conn.close();
}

// A roll to an older epoch must not make fenced authority current again.
void scenario_epoch_monotonic() {
  Cluster cl;
  start_coordinator(cl, "epoch");
  HANDLE w = start_worker(cl, 1, 0x4444, 4);
  dist::TcpConnection conn;
  CHECK_TRUE(conn.connect("127.0.0.1", cl.port));
  wait_registered(conn, w, 0x4444);
  roll(conn, 3);
  auto key_a = make_key("epoch_a", 512);
  CHECK_EQ(lookup(conn, make_auth(3, 3, 0x4444, 4), RequestId(8, 1), key_a), 0x0B);
  roll(conn, 2);
  CHECK_EQ(lookup(conn, make_auth(2, 2, 0x4444, 4), RequestId(8, 2), make_key("epoch_b", 512)), 0x0D);
  CHECK_EQ(lookup(conn, make_auth(3, 3, 0x4444, 4), RequestId(8, 3), key_a), 0x00);
  conn.close();
}

// A truncated RollEpoch frame is ignored instead of resetting the epoch to 0.
void scenario_malformed_roll() {
  Cluster cl;
  start_coordinator(cl, "malformed");
  HANDLE w = start_worker(cl, 1, 0x5555, 5);
  dist::TcpConnection conn;
  CHECK_TRUE(conn.connect("127.0.0.1", cl.port));
  wait_registered(conn, w, 0x5555);
  CHECK_TRUE(conn.write_frame(static_cast<std::uint16_t>(DistMsgType::RollEpoch), std::vector<std::uint8_t>{}));
  auto auth = make_auth(1, 1, 0x5555, 5);
  auto key = make_key("malformed_roll", 1024);
  CHECK_EQ(lookup(conn, auth, RequestId(9, 1), key), 0x0B);
  CHECK_EQ(lookup(conn, auth, RequestId(9, 2), key), 0x00);
  conn.close();
}

// Concurrent clients building distinct keys while another client keeps rolling
// the (unchanged) epoch: every lookup is answered and every build succeeds.
void scenario_concurrent_clients() {
  Cluster cl;
  start_coordinator(cl, "concurrent");
  HANDLE wa = start_worker(cl, 1, 0x6666, 6);
  HANDLE wb = start_worker(cl, 2, 0x7777, 7);
  dist::TcpConnection ctl;
  CHECK_TRUE(ctl.connect("127.0.0.1", cl.port));
  wait_registered(ctl, wa, 0x6666);
  wait_registered(ctl, wb, 0x7777);
  const int kClients = 8;
  const int kKeys = 20;
  std::atomic<int> failures{0};
  std::atomic<bool> clients_done{false};
  std::vector<std::thread> clients;
  for (int c = 0; c < kClients; ++c) {
    clients.emplace_back([&, c] {
      dist::TcpConnection conn;
      if (!conn.connect("127.0.0.1", cl.port)) { ++failures; return; }
      for (int i = 0; i < kKeys; ++i) {
        const bool first = ((c + i) % 2) == 0;
        auto auth = first ? make_auth(1, 1, 0x6666, 6) : make_auth(1, 1, 0x7777, 7);
        auto key = make_key("cc_" + std::to_string(c) + "_" + std::to_string(i), 256 + i);
        const std::uint64_t rq = static_cast<std::uint64_t>(c) * 1000u + static_cast<std::uint64_t>(i);
        if (lookup(conn, auth, RequestId(rq, 1), key) != 0x0B) ++failures;
        if (lookup(conn, auth, RequestId(rq, 2), key) != 0x00) ++failures;
      }
      conn.close();
    });
  }
  std::thread roller([&] {
    dist::TcpConnection conn;
    if (!conn.connect("127.0.0.1", cl.port)) { ++failures; return; }
    while (!clients_done.load()) { roll(conn, 1); std::this_thread::yield(); }
    if (query_workers(conn).size() != 2u) ++failures;
    conn.close();
  });
  for (auto& t : clients) t.join();
  clients_done = true;
  roller.join();
  if (failures.load() != 0) std::cerr << "concurrent_clients failures=" << failures.load() << "\n";
  CHECK_EQ(failures.load(), 0);
  auto boots = query_workers(ctl);
  CHECK_TRUE(listed(boots, 0x6666));
  CHECK_TRUE(listed(boots, 0x7777));
  ctl.close();
}

}  // namespace

int main(int argc, char** argv) {
  struct Scenario { const char* name; void (*fn)(); };
  const Scenario all[] = {
    {"worker_restart", scenario_worker_restart},
    {"epoch_monotonic", scenario_epoch_monotonic},
    {"malformed_roll", scenario_malformed_roll},
    {"concurrent_clients", scenario_concurrent_clients},
  };
  int ran = 0;
  for (const auto& s : all) {
    if (argc > 1 && std::strcmp(argv[1], s.name) != 0) continue;
    s.fn();
    std::cout << "scenario " << s.name << " OK\n";
    ++ran;
  }
  if (ran == 0) { std::cerr << "unknown scenario\n"; return 2; }
  std::cout << "DIST_REGRESSION_OK scenarios=" << ran << " checks=" << kctest::g_checks << "\n";
  return 0;
}
#else
int main() {
  std::printf("DIST_REGRESSION_SKIPPED (the process harness is Windows-only)\n");
  return 0;
}
#endif
