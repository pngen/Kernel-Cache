// dist_protocol.cpp - framed transport codec + authority validation.
#include "kernelcache/distributed.hpp"
#include "test_util.hpp"
#include <iostream>

using namespace kernelcache;

int main() {
  // Frame roundtrip.
  std::vector<std::uint8_t> payload = {1,2,3,4,5};
  auto f = encode_frame(DistMsgType::LookupReq, payload);
  CHECK_EQ(f.size(), kDistHeaderBytes + payload.size());
  auto d = decode_frame(std::span<const std::uint8_t>(f.data(), f.size()));
  CHECK_TRUE(d.ok());
  CHECK_EQ(d.value().first, DistMsgType::LookupReq);
  CHECK_TRUE(d.value().second == payload);

  // Oversized frame rejected.
  std::vector<std::uint8_t> big(kDistMaxFrameBytes + 16, 0);
  // craft a frame header claiming huge length
  std::vector<std::uint8_t> ov = {0xFF,0xFF,0xFF,0xFF,0,0,0,0};
  auto ob = decode_frame(std::span<const std::uint8_t>(ov.data(), ov.size()));
  CHECK_TRUE(!ob.ok());
  CHECK_EQ(ob.error().code(), ErrorCode::FrameTooLarge);

  // Truncated (length mismatch) rejected.
  std::vector<std::uint8_t> trunc = {0,0,0,20, 0,0, 0,3, 9,9};
  auto tb = decode_frame(std::span<const std::uint8_t>(trunc.data(), trunc.size()));
  CHECK_TRUE(!tb.ok());
  CHECK_EQ(tb.error().code(), ErrorCode::TruncatedData);

  // Unknown protocol version rejected.
  std::vector<std::uint8_t> uv = {0,0,0,8, 0,99, 0,4};
  auto ub = decode_frame(std::span<const std::uint8_t>(uv.data(), uv.size()));
  CHECK_TRUE(!ub.ok());
  CHECK_EQ(ub.error().code(), ErrorCode::ProtocolVersionMismatch);

  // Unknown message type rejected.
  std::vector<std::uint8_t> ut = {0,0,0,8, 0,1, 0x7F,0xFF};
  auto utb = decode_frame(std::span<const std::uint8_t>(ut.data(), ut.size()));
  CHECK_TRUE(!utb.ok());
  CHECK_EQ(utb.error().code(), ErrorCode::UnknownMessageType);

  // Authority is lossless for 64-bit values.
  DistAuthority a;
  a.epoch.value = 0x8000000000000001ULL;
  a.worker.value = 0x00000000FFFFFFFFULL;
  a.boot = WorkerBootId{StrongId128(0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL)};
  a.cache_gen.value = 0xFFFFFFFFFFFFFFFFULL;
  a.artifact_gen.value = 0x0000000100000002ULL;
  a.attempt = BuildAttemptId(0xAABBCCDDEEFF0011ULL, 0x2233445566778899ULL);
  a.request = RequestId(0x1111222233334444ULL, 0x5555666677778888ULL);
  std::vector<std::uint8_t> enc;
  CHECK_TRUE(encode_authority(enc, a));
  std::size_t pos = 0;
  DistAuthority b;
  CHECK_TRUE(decode_authority(enc, pos, b));
  CHECK_EQ(a.epoch.value, b.epoch.value);
  CHECK_EQ(a.worker.value, b.worker.value);
      CHECK_EQ(a.boot.value.hi(), b.boot.value.hi());
  CHECK_EQ(a.boot.value.lo(), b.boot.value.lo());
  CHECK_EQ(a.cache_gen.value, b.cache_gen.value);
  CHECK_EQ(a.attempt.hi(), b.attempt.hi());
  CHECK_EQ(a.attempt.lo(), b.attempt.lo());

  // Lookup request/response roundtrip with lossless 64-bit request id.
  RequestId rid(0x8765432101234567ULL, 0x890ABCDEF1234567ULL);
  std::vector<std::uint8_t> keycan = {0xDE,0xAD,0xBE,0xEF};
  auto lr = encode_lookup_request(a, rid, "vec_add", keycan, "ns", 3);
  RequestId rid2; std::string op2; std::vector<std::uint8_t> kc2; std::string ns2; std::uint8_t dt2;
  auto ldr = decode_lookup_request(lr, rid2, op2, kc2, ns2, dt2);
  CHECK_TRUE(ldr.ok());
  CHECK_EQ(rid.hi(), rid2.hi()); CHECK_EQ(rid.lo(), rid2.lo());
  CHECK_EQ(op2, "vec_add"); CHECK_TRUE(kc2 == keycan); CHECK_EQ(ns2, "ns"); CHECK_EQ(dt2, 3);

  ArtifactId art(0x12345678ULL, 0x9ABCDEF0ULL);
  auto resp = encode_lookup_response(rid, 3, art, ArtifactGeneration{7}, 1);
  RequestId rr2; std::uint8_t out; ArtifactId id2; ArtifactGeneration g2; std::uint8_t compat;
  CHECK_TRUE(decode_lookup_response(resp, rr2, out, id2, g2, compat).ok());
  CHECK_EQ(id2.hi(), art.hi()); CHECK_EQ(id2.lo(), art.lo()); CHECK_EQ(g2.value, 7u); CHECK_EQ(out, 3);

  std::cout << "DIST_PROTOCOL_OK checks=" << kctest::g_checks << "\n";
  return 0;
}