#define _CRT_SECURE_NO_WARNINGS
// worker.cpp - a real OS cache-worker process. Registers with the coordinator,
// receives BuildReq frames over real TCP, checks authority, builds via the CPU
// backend, and returns BuildResp.
#include "kernelcache/kernelcache.hpp"
#include "kernelcache/distributed.hpp"
#include "dist_tcp.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace kernelcache;

int main(int argc, char** argv) {
  if (argc < 6) { std::fprintf(stderr, "usage: worker <host> <port> <workerId> <bootHi> <bootLo>\n"); return 2; }
  std::string host = argv[1];
  std::uint16_t port = static_cast<std::uint16_t>(std::atoi(argv[2]));
  std::uint64_t worker_id = std::strtoull(argv[3], nullptr, 10);
  std::uint64_t boot_hi = std::strtoull(argv[4], nullptr, 10);
  std::uint64_t boot_lo = std::strtoull(argv[5], nullptr, 10);
  WorkerBootId my_boot{StrongId128(boot_hi, boot_lo)};

  dist::TcpConnection conn;
  conn.connect(host, port);
  // Register.
  auto reg = encode_register(WorkerId(worker_id), my_boot, CacheGeneration{1});
  conn.write_frame(static_cast<std::uint16_t>(DistMsgType::Register_W), reg);
  std::uint16_t type; std::vector<std::uint8_t> payload;
  if (!conn.read_frame(type, payload) || type != static_cast<std::uint16_t>(DistMsgType::RegisterAck_C)) {
    std::fprintf(stderr, "worker: register ack failed\n"); return 3;
  }
  CoordinatorEpoch coord_epoch; WorkerId wid; std::uint8_t ok = 0;
  if (!decode_register_ack(payload, coord_epoch, wid, ok).ok() || ok != 1) {
    std::fprintf(stderr, "worker: register rejected\n"); return 3;
  }
  std::fprintf(stderr, "worker[%llu] registered epoch=%llu\n", (unsigned long long)worker_id, (unsigned long long)coord_epoch.value);

  KernelCacheConfig cfg; cfg.persistence_root = std::string(getenv("TEMP") && getenv("TEMP")[0] ? getenv("TEMP") : "\\tmp") + "/kc_worker_" + std::to_string(worker_id);
  cfg.persistence_enabled = false;
  KernelCache cache(cfg);
  cache.use_builtin_backends();

  for (;;) {
    if (!conn.read_frame(type, payload)) break;
    if (type == static_cast<std::uint16_t>(DistMsgType::RollEpoch)) {
      CoordinatorEpoch e; if (decode_roll_epoch(payload, e)) coord_epoch.value = e.value;
      continue;
    }
    if (type == static_cast<std::uint16_t>(DistMsgType::Shutdown)) break;
    if (type != static_cast<std::uint16_t>(DistMsgType::BuildReq)) continue;
    RequestId rid; std::vector<std::uint8_t> keycan; std::string src, ns, arch;
    auto a = decode_build_request(payload, rid, keycan, src, ns, arch);
    ArtifactId id; ArtifactGeneration gen; std::uint32_t errcode = 0; std::string err;
    if (!a) { errcode = static_cast<std::uint32_t>(ErrorCode::MalformedFrame); err = "malformed build request"; }
    else if (a.value().epoch.value != coord_epoch.value) { errcode = static_cast<std::uint32_t>(ErrorCode::EpochMismatch); err = "stale epoch"; }
    else if (a.value().boot.value != my_boot.value) { errcode = static_cast<std::uint32_t>(ErrorCode::WorkerBootMismatch); err = "stale worker boot"; }
    else {
      // Build via the CPU backend.
      auto k = KernelCompatibilityKey::from_canonical(std::span<const std::uint8_t>(keycan.data(), keycan.size()));
      if (!k) { errcode = static_cast<std::uint32_t>(ErrorCode::MalformedFrame); err = "bad key"; }
      else {
        KernelLookupRequest req;
        req.key = k.value();
        req.device.vendor = DeviceVendor::CPU; req.device.family = AcceleratorFamily::CPU;
        req.desired_tier = ResidencyTier::HostResident;
        req.allow_miss_build = true;
        req.source = src;
        req.namespace_ = ns;
        auto l = cache.acquire(req);
        if (l.ok()) { id = l.value().artifact_id(); gen = l.value().generation(); l.value().release(); }
        else { errcode = static_cast<std::uint32_t>(ErrorCode::BuildFailed); err = l.error().message(); std::fprintf(stderr, "worker[%llu] build FAIL code=%s msg='%s'\n", (unsigned long long)worker_id, error_code_name(l.error().code()), l.error().message().c_str()); }
      }
    }
    auto resp = encode_build_response(rid, id, gen, errcode, err);
    conn.write_frame(static_cast<std::uint16_t>(DistMsgType::BuildResp), resp);
    if (type == static_cast<std::uint16_t>(DistMsgType::Shutdown)) break;
  }
  conn.close();
  std::fprintf(stderr, "worker[%llu] exiting\n", (unsigned long long)worker_id);
  return 0;
}