// client.cpp - a real client/driver that connects to the coordinator over framed
// TCP and performs a lookup. Used by examples and the CLI.
#include "kernelcache/kernelcache.hpp"
#include "kernelcache/distributed.hpp"
#include "dist_tcp.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace kernelcache;

int main(int argc, char** argv) {
  if (argc < 4) { std::fprintf(stderr, "usage: client <host> <port> <operation> [shape]\n"); return 2; }
  std::string host = argv[1];
  std::uint16_t port = static_cast<std::uint16_t>(std::atoi(argv[2]));
  std::string op = argv[3];
  std::int64_t shape = argc > 4 ? std::atoll(argv[4]) : 1024;

  KernelCompatibilityKey k;
  k.operation = op; k.artifact_format = "kc-synth";
  k.vendor = DeviceVendor::CPU; k.family = AcceleratorFamily::CPU; k.arch = "x86_64";
  k.runtime_backend = "cpu-synth"; k.compiler_name = "msvc"; k.compiler_version_major = 19; k.compiler_version_minor = 44;
  k.abi_name = "kc-synth-1.0"; k.abi_version = 1; k.kernel_interface_signature = "void(float*,const float*,uint64)";
  k.dtypes = {Datatype::F32}; k.layouts = {TensorLayout::RowMajor}; k.rank = 1; k.shape = {shape};
  k.alignment_bytes = 16; k.block_x = 256; k.launch_abi = "grid-block"; k.finalize();

  DistAuthority auth;
  auth.epoch.value = 1; auth.worker.value = 0; auth.boot = WorkerBootId{};
  auth.cache_gen.value = 1; auth.artifact_gen.value = 0; auth.attempt = BuildAttemptId{};
  auth.request = RequestId{};
  RequestId rid(0x1234, 0x5678);

  dist::TcpConnection conn;
  if (!conn.connect(host, port)) { std::fprintf(stderr, "client: connect failed\n"); return 1; }
  auto req = encode_lookup_request(auth, rid, op,
      std::vector<std::uint8_t>(k.canonical_bytes().begin(), k.canonical_bytes().end()), "", 3);
  conn.write_frame(static_cast<std::uint16_t>(DistMsgType::LookupReq), req);
  std::uint16_t t; std::vector<std::uint8_t> pl;
  if (!conn.read_frame(t, pl) || t != static_cast<std::uint16_t>(DistMsgType::LookupResp)) { std::fprintf(stderr, "client: no response\n"); return 1; }
  RequestId rr; std::uint8_t outcome; ArtifactId id; ArtifactGeneration gen; std::uint8_t compat;
  if (!decode_lookup_response(pl, rr, outcome, id, gen, compat).ok()) { std::fprintf(stderr, "client: bad resp\n"); return 1; }
  std::printf("client: outcome=0x%02X op=%s id=%s gen=%llu\n", outcome, op.c_str(), id.str().c_str(), (unsigned long long)gen.value);
  conn.close();
  return (outcome == 0x00) ? 0 : 1;
}
