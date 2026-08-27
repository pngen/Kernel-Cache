// coordinator.cpp - a real OS cache-coordinator process.
// Accepts connections; each connection is handled in its own thread so worker
// registrations are processed even while one client is being served (this is
// what makes a post-restart worker usable mid-scenario without a race).
#include "kernelcache/kernelcache.hpp"
#include "kernelcache/distributed.hpp"
#include "dist_tcp.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <set>
#include <unordered_map>
#include <utility>

using namespace kernelcache;

struct WorkerSlot { kc_socket_t fd = KC_INVALID_SOCKET; WorkerId id; WorkerBootId boot; };

static std::mutex g_wmtx;              // guards g_workers
static std::vector<WorkerSlot> g_workers;
static std::mutex g_dispatch_mtx;      // serializes dispatch to a shared worker socket
static std::atomic<std::uint64_t> g_epoch{1};
static std::atomic<std::uint64_t> g_gen{1};
static KernelCache* g_cache = nullptr;
static std::set<std::string> g_pending;   // canonical keys that missed and are awaiting a build
static std::unordered_map<std::string, std::pair<ArtifactId, ArtifactGeneration>> g_built;   // keys the coordinator has built

static bool dispatch_build(const std::vector<std::uint8_t>& keycan, const std::string& src,
                           const std::string& ns, const std::string& arch, const DistAuthority& auth,
                           RequestId rid, ArtifactId& oid, ArtifactGeneration& ogen,
                           std::uint32_t& errc, std::string& errm) {
  std::lock_guard<std::mutex> dlk(g_dispatch_mtx);
  WorkerSlot w;
  {
    std::lock_guard<std::mutex> lk(g_wmtx);
    for (auto& s : g_workers) {
      if (s.fd != KC_INVALID_SOCKET && s.boot.value == auth.boot.value) { w = s; break; }
    }
    if (w.fd == KC_INVALID_SOCKET) {
      for (auto& s : g_workers) { if (s.fd != KC_INVALID_SOCKET) { w = s; break; } }
    }
  }
  if (w.fd == KC_INVALID_SOCKET) { errc = static_cast<std::uint32_t>(ErrorCode::NotSupported); errm = "no worker"; return false; }
  dist::TcpConnection c; c.fd = w.fd;
  auto req = encode_build_request(auth, rid, keycan, src, ns, arch);
  if (!c.write_frame(static_cast<std::uint16_t>(DistMsgType::BuildReq), req)) return false;
  std::uint16_t t; std::vector<std::uint8_t> pl;
  if (!c.read_frame(t, pl) || t != static_cast<std::uint16_t>(DistMsgType::BuildResp)) return false;
  RequestId rr; decode_build_response(pl, rr, oid, ogen, errc, errm);
  return errc == 0;
}

// Serve one client connection: process its frames until it closes.
static void serve_client(dist::TcpConnection conn, std::uint16_t first_type, std::vector<std::uint8_t> first_payload) {
  std::uint16_t type = first_type;
  std::vector<std::uint8_t> payload = std::move(first_payload);
  for (;;) {
    if (type == static_cast<std::uint16_t>(DistMsgType::RollEpoch)) {
      CoordinatorEpoch e; decode_roll_epoch(payload, e);
      g_epoch.store(e.value); g_gen.store(e.value);
      std::lock_guard<std::mutex> lk(g_wmtx);
      for (auto& s : g_workers) {
        if (s.fd != KC_INVALID_SOCKET) { dist::TcpConnection wc; wc.fd = s.fd; auto rf = encode_roll_epoch(e); (void)wc.write_frame(static_cast<std::uint16_t>(DistMsgType::RollEpoch), rf); }
      }
    } else if (type == static_cast<std::uint16_t>(DistMsgType::LookupReq)) {
      RequestId rid; std::string op; std::vector<std::uint8_t> keycan; std::string ns; std::uint8_t tier;
      auto auth = decode_lookup_request(payload, rid, op, keycan, ns, tier);
      std::uint8_t outcome = 0x0B; ArtifactId id; ArtifactGeneration gen; std::uint8_t compat = 0;
      if (!auth) outcome = 0x0C;
      else if (auth.value().epoch.value != g_epoch.load()) outcome = 0x0D;
      else if (auth.value().cache_gen.value != g_gen.load()) outcome = 0x0E;
      else {
        auto k = KernelCompatibilityKey::from_canonical(std::span<const std::uint8_t>(keycan.data(), keycan.size()));
        if (!k) outcome = 0x0C;
        else {
          KernelLookupRequest req; req.key = k.value(); req.device.vendor = DeviceVendor::CPU;
          req.desired_tier = ResidencyTier::HostResident; req.allow_miss_build = false; req.namespace_ = ns;
          auto lk = g_cache->lookup(req);
          std::string kh = k.value().digest_hex();
          auto bit = g_built.find(kh);
          if (bit != g_built.end()) { id = bit->second.first; gen = bit->second.second; outcome = 0x00; compat = 1; }
          else if (lk.ok() && lk.value().is_hit()) { id = *lk.value().artifact; gen = *lk.value().generation; outcome = 0x00; compat = 1; }
          else if (g_pending.count(kh)) {
            DistAuthority wa = *auth;
            ArtifactId bid; ArtifactGeneration bgen; std::uint32_t berr = 0; std::string bemsg;
            bool ok = dispatch_build(keycan, std::string(), ns, k.value().arch, wa, rid, bid, bgen, berr, bemsg);
            g_pending.erase(kh);
            if (ok) { id = bid; gen = bgen; outcome = 0x00; compat = 1; g_built[kh] = {bid, bgen}; }
            else { outcome = 0x0F; }
          } else { g_pending.insert(kh); outcome = 0x0B; }
        }
      }
      auto resp = encode_lookup_response(rid, outcome, id, gen, compat);
      conn.write_frame(static_cast<std::uint16_t>(DistMsgType::LookupResp), resp);
    } else if (type == static_cast<std::uint16_t>(DistMsgType::QueryWorkers)) {
      std::vector<std::uint64_t> boots;
      { std::lock_guard<std::mutex> lk(g_wmtx); for (auto& s : g_workers) if (s.fd != KC_INVALID_SOCKET) boots.push_back(s.boot.value.hi()); }
      auto resp = encode_query_workers_resp(boots);
      conn.write_frame(static_cast<std::uint16_t>(DistMsgType::QueryWorkersResp), resp);
    } else if (type == static_cast<std::uint16_t>(DistMsgType::Shutdown)) { conn.close(); break; }
    if (!conn.read_frame(type, payload)) break;
  }
  conn.close();
}

// Handle a single accepted connection (worker registration or client session).
static void handle_conn(kc_socket_t sock) {
  dist::TcpConnection conn; conn.fd = sock;
  std::uint16_t type; std::vector<std::uint8_t> payload;
  if (!conn.read_frame(type, payload)) { conn.close(); return; }
  if (type == static_cast<std::uint16_t>(DistMsgType::Register_W)) {
    CacheGeneration g;
    auto reg = decode_register(payload, g);
    if (!reg) { conn.close(); return; }
    WorkerId wid = reg.value().first; WorkerBootId boot = reg.value().second;
    {
      std::lock_guard<std::mutex> lk(g_wmtx);
      g_workers.push_back({sock, wid, boot});
    }
    dist::TcpConnection ackconn; ackconn.fd = sock;
    auto ack = encode_register_ack(CoordinatorEpoch(g_epoch.load()), wid, 1);
    ackconn.write_frame(static_cast<std::uint16_t>(DistMsgType::RegisterAck_C), ack);

    // Thread exits; the socket stays open and is owned by g_workers for dispatch.
    return;
  }
  serve_client(conn, type, std::move(payload));
}

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: coordinator <port> <cacheRoot>\n"); return 2; }
  std::uint16_t port = static_cast<std::uint16_t>(std::atoi(argv[1]));
  KernelCacheConfig cfg; cfg.persistence_root = argv[2]; cfg.persistence_enabled = true;
  KernelCache cache(cfg); cache.use_builtin_backends();
  g_cache = &cache;
  dist::TcpListener listen;
  if (!listen.listen(port)) { std::fprintf(stderr, "coord: bind failed\n"); return 3; }
  std::fprintf(stderr, "coord: listening port=%u\n", port);
  for (;;) {
    auto sock = listen.accept();
    if (sock == KC_INVALID_SOCKET) continue;
    std::thread(handle_conn, sock).detach();
  }
  return 0;
}