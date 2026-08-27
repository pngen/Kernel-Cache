#include "kernelcache/distributed.hpp"
#include "kernelcache/canonical.hpp"

namespace kernelcache {

const char* dist_msg_type_name(DistMsgType t) noexcept {
  switch (t) {
    case DistMsgType::Hello_W: return "hello-w";
    case DistMsgType::Register_W: return "register-w";
    case DistMsgType::RegisterAck_C: return "register-ack-c";
    case DistMsgType::LookupReq: return "lookup-req";
    case DistMsgType::LookupResp: return "lookup-resp";
    case DistMsgType::BuildReq: return "build-req";
    case DistMsgType::BuildResp: return "build-resp";
    case DistMsgType::InvalidateReq: return "invalidate-req";
    case DistMsgType::InvalidateResp: return "invalidate-resp";
    case DistMsgType::RollEpoch: return "roll-epoch";
    case DistMsgType::RollEpochAck: return "roll-epoch-ack";
    case DistMsgType::EpochInvalid: return "epoch-invalid";
    case DistMsgType::Shutdown: return "shutdown";
    case DistMsgType::QueryWorkers: return "query-workers";
    case DistMsgType::QueryWorkersResp: return "query-workers-resp";
  }
  return "unknown";
}

namespace {
struct CursorDead {};
struct Reader {
  const std::uint8_t* p = nullptr; std::size_t n = 0; std::size_t i = 0;
  bool nbytes(std::size_t k) const { return i + k <= n; }
  std::uint8_t u8() { if (!nbytes(1)) throw CursorDead{}; return p[i++]; }
  std::uint16_t u16() { std::uint16_t v = static_cast<std::uint16_t>(u8()) << 8; v |= u8(); return v; }
  std::uint32_t u32() { std::uint32_t v = 0; for (int k = 3; k >= 0; --k) v = (v << 8) | u8(); return v; }
  std::uint64_t u64() { std::uint64_t v = 0; for (int k = 7; k >= 0; --k) v = (v << 8) | u8(); return v; }
  std::string str() { std::uint64_t len = u64(); if (len > n - i) throw CursorDead{}; std::string s(reinterpret_cast<const char*>(p + i), static_cast<std::size_t>(len)); i += static_cast<std::size_t>(len); return s; }
  std::vector<std::uint8_t> bytes() { std::uint64_t len = u64(); if (len > n - i) throw CursorDead{}; std::vector<std::uint8_t> v(p + i, p + i + len); i += static_cast<std::size_t>(len); return v; }
};
template <typename T>
void append_u64_be(std::vector<T>& v, std::uint64_t x) { v.push_back(static_cast<T>((x >> 56) & 0xff)); v.push_back(static_cast<T>((x >> 48) & 0xff)); v.push_back(static_cast<T>((x >> 40) & 0xff)); v.push_back(static_cast<T>((x >> 32) & 0xff)); v.push_back(static_cast<T>((x >> 24) & 0xff)); v.push_back(static_cast<T>((x >> 16) & 0xff)); v.push_back(static_cast<T>((x >> 8) & 0xff)); v.push_back(static_cast<T>(x & 0xff)); }
}  // namespace

std::vector<std::uint8_t> encode_frame(DistMsgType type, const std::vector<std::uint8_t>& payload) {
  std::uint64_t total = kDistHeaderBytes + payload.size();
  std::vector<std::uint8_t> out(static_cast<std::size_t>(total), 0);
  out[0] = static_cast<std::uint8_t>((total >> 24) & 0xff);
  out[1] = static_cast<std::uint8_t>((total >> 16) & 0xff);
  out[2] = static_cast<std::uint8_t>((total >> 8) & 0xff);
  out[3] = static_cast<std::uint8_t>(total & 0xff);
  out[4] = static_cast<std::uint8_t>((kDistProtoVersion >> 8) & 0xff);
  out[5] = static_cast<std::uint8_t>(kDistProtoVersion & 0xff);
  out[6] = static_cast<std::uint8_t>((static_cast<std::uint16_t>(type) >> 8) & 0xff);
  out[7] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(type) & 0xff);
  std::copy(payload.begin(), payload.end(), out.begin() + kDistHeaderBytes);
  return out;
}

Result<std::pair<DistMsgType, std::vector<std::uint8_t>>> decode_frame(std::span<const std::uint8_t> frame) {
  if (frame.size() < kDistHeaderBytes) return Result<std::pair<DistMsgType, std::vector<std::uint8_t>>>(ErrorCode::MalformedFrame, "frame shorter than header");
  std::uint32_t len = (static_cast<std::uint32_t>(frame[0]) << 24) | (static_cast<std::uint32_t>(frame[1]) << 16) | (static_cast<std::uint32_t>(frame[2]) << 8) | frame[3];
  std::uint16_t ver = (static_cast<std::uint16_t>(frame[4]) << 8) | frame[5];
  std::uint16_t type16 = (static_cast<std::uint16_t>(frame[6]) << 8) | frame[7];
  if (len > kDistMaxFrameBytes) return Result<std::pair<DistMsgType, std::vector<std::uint8_t>>>(ErrorCode::FrameTooLarge, "frame exceeds hard maximum");
  if (len != frame.size()) return Result<std::pair<DistMsgType, std::vector<std::uint8_t>>>(ErrorCode::TruncatedData, "frame length mismatch (truncated or partial)");
  if (ver != kDistProtoVersion) return Result<std::pair<DistMsgType, std::vector<std::uint8_t>>>(ErrorCode::ProtocolVersionMismatch, "unsupported protocol version");
  DistMsgType type = static_cast<DistMsgType>(type16);
  bool known = false;
  for (auto t : {DistMsgType::Hello_W, DistMsgType::Register_W, DistMsgType::RegisterAck_C, DistMsgType::LookupReq, DistMsgType::LookupResp, DistMsgType::BuildReq, DistMsgType::BuildResp, DistMsgType::InvalidateReq, DistMsgType::InvalidateResp, DistMsgType::RollEpoch, DistMsgType::RollEpochAck, DistMsgType::EpochInvalid, DistMsgType::Shutdown, DistMsgType::QueryWorkers, DistMsgType::QueryWorkersResp}) if (t == type) known = true;
  if (!known) return Result<std::pair<DistMsgType, std::vector<std::uint8_t>>>(ErrorCode::UnknownMessageType, "unknown message type");
  std::vector<std::uint8_t> payload(frame.begin() + kDistHeaderBytes, frame.end());
  return std::make_pair(type, std::move(payload));
}

bool encode_authority(std::vector<std::uint8_t>& out, const DistAuthority& a) {
  out.push_back(1);  // authority marker
  append_u64_be(out, a.epoch.value);
  append_u64_be(out, a.worker.value);
  append_u64_be(out, a.boot.value.hi());
  append_u64_be(out, a.boot.value.lo());
  append_u64_be(out, a.cache_gen.value);
  append_u64_be(out, a.artifact_gen.value);
  append_u64_be(out, a.attempt.hi());
  append_u64_be(out, a.attempt.lo());
  append_u64_be(out, a.request.hi());
  append_u64_be(out, a.request.lo());
  return true;
}
bool decode_authority(const std::vector<std::uint8_t>& payload, std::size_t& pos, DistAuthority& a) {
  try {
    Reader r{payload.data(), payload.size(), pos};
    if (r.u8() != 1) return false;
    a.epoch.value = r.u64();
    a.worker.value = r.u64();
    std::uint64_t bh = r.u64(); std::uint64_t bl = r.u64();
    a.boot = WorkerBootId{StrongId128(bh, bl)};
    a.cache_gen.value = r.u64();
    a.artifact_gen.value = r.u64();
    std::uint64_t ah = r.u64(); std::uint64_t al = r.u64(); a.attempt = BuildAttemptId(ah, al);
    std::uint64_t rh = r.u64(); std::uint64_t rl = r.u64(); a.request = RequestId(rh, rl);
    pos = r.i;
    return true;
  } catch (const CursorDead&) { return false; }
}

std::vector<std::uint8_t> encode_lookup_request(const DistAuthority& a, RequestId rid, const std::string& op,
                                                const std::vector<std::uint8_t>& key_canonical,
                                                const std::string& namespace_, std::uint8_t desired_tier) {
  std::vector<std::uint8_t> out;
  encode_authority(out, a);
  append_u64_be(out, rid.hi()); append_u64_be(out, rid.lo());
  append_u64_be(out, static_cast<std::uint64_t>(op.size()));
  out.insert(out.end(), op.begin(), op.end());
  append_u64_be(out, static_cast<std::uint64_t>(key_canonical.size()));
  out.insert(out.end(), key_canonical.begin(), key_canonical.end());
  append_u64_be(out, static_cast<std::uint64_t>(namespace_.size()));
  out.insert(out.end(), namespace_.begin(), namespace_.end());
  out.push_back(desired_tier);
  return out;
}
Result<DistAuthority> decode_lookup_request(const std::vector<std::uint8_t>& p, RequestId& rid, std::string& op,
                                            std::vector<std::uint8_t>& key_canonical, std::string& namespace_, std::uint8_t& desired_tier) {
  try {
    Reader r{p.data(), p.size(), 0};
    DistAuthority a;
    if (r.u8() != 1) return Result<DistAuthority>(ErrorCode::MalformedFrame, "bad authority marker");
    a.epoch.value = r.u64(); a.worker.value = r.u64();
    std::uint64_t b0 = r.u64(); std::uint64_t b1 = r.u64(); a.boot = WorkerBootId{StrongId128(b0, b1)};
    a.cache_gen.value = r.u64(); a.artifact_gen.value = r.u64();
    std::uint64_t at0 = r.u64(); std::uint64_t at1 = r.u64(); a.attempt = BuildAttemptId(at0, at1);
    std::uint64_t rq0 = r.u64(); std::uint64_t rq1 = r.u64(); a.request = RequestId(rq0, rq1);
    std::uint64_t rd0 = r.u64(); std::uint64_t rd1 = r.u64(); rid = RequestId(rd0, rd1);
    op = r.str(); key_canonical = r.bytes(); namespace_ = r.str();
    desired_tier = r.u8();
    return a;
  } catch (const CursorDead&) { return Result<DistAuthority>(ErrorCode::MalformedFrame, "truncated lookup request"); }
}

std::vector<std::uint8_t> encode_lookup_response(RequestId rid, std::uint8_t outcome, ArtifactId id, ArtifactGeneration gen, std::uint8_t compatible) {
  std::vector<std::uint8_t> out;
  append_u64_be(out, rid.hi()); append_u64_be(out, rid.lo());
  out.push_back(outcome); out.push_back(compatible);
  append_u64_be(out, id.hi()); append_u64_be(out, id.lo());
  append_u64_be(out, gen.value);
  return out;
}
Result<void> decode_lookup_response(const std::vector<std::uint8_t>& p, RequestId& rid, std::uint8_t& outcome, ArtifactId& id, ArtifactGeneration& gen, std::uint8_t& compatible) {
  try {
    Reader r{p.data(), p.size(), 0};
    std::uint64_t rd0 = r.u64(); std::uint64_t rd1 = r.u64(); rid = RequestId(rd0, rd1);
    outcome = r.u8(); compatible = r.u8();
    std::uint64_t id0 = r.u64(); std::uint64_t id1 = r.u64(); id = ArtifactId(id0, id1);
    gen.value = r.u64();
    return Result<void>();
  } catch (const CursorDead&) { return Result<void>(ErrorCode::MalformedFrame, "truncated lookup response"); }
}

std::vector<std::uint8_t> encode_build_request(const DistAuthority& a, RequestId rid, const std::vector<std::uint8_t>& key_canonical, const std::string& source, const std::string& namespace_, const std::string& arch) {
  std::vector<std::uint8_t> out;
  encode_authority(out, a);
  append_u64_be(out, rid.hi()); append_u64_be(out, rid.lo());
  append_u64_be(out, static_cast<std::uint64_t>(key_canonical.size())); out.insert(out.end(), key_canonical.begin(), key_canonical.end());
  append_u64_be(out, static_cast<std::uint64_t>(source.size())); out.insert(out.end(), source.begin(), source.end());
  append_u64_be(out, static_cast<std::uint64_t>(namespace_.size())); out.insert(out.end(), namespace_.begin(), namespace_.end());
  append_u64_be(out, static_cast<std::uint64_t>(arch.size())); out.insert(out.end(), arch.begin(), arch.end());
  return out;
}
Result<DistAuthority> decode_build_request(const std::vector<std::uint8_t>& p, RequestId& rid, std::vector<std::uint8_t>& key_canonical, std::string& source, std::string& namespace_, std::string& arch) {
  try {
    Reader r{p.data(), p.size(), 0};
    if (r.u8() != 1) return Result<DistAuthority>(ErrorCode::MalformedFrame, "bad authority marker");
    DistAuthority a;
    a.epoch.value = r.u64(); a.worker.value = r.u64();
    std::uint64_t b0 = r.u64(); std::uint64_t b1 = r.u64(); a.boot = WorkerBootId{StrongId128(b0, b1)};
    a.cache_gen.value = r.u64(); a.artifact_gen.value = r.u64();
    std::uint64_t at0 = r.u64(); std::uint64_t at1 = r.u64(); a.attempt = BuildAttemptId(at0, at1);
    std::uint64_t rq0 = r.u64(); std::uint64_t rq1 = r.u64(); a.request = RequestId(rq0, rq1);
    std::uint64_t rd0 = r.u64(); std::uint64_t rd1 = r.u64(); rid = RequestId(rd0, rd1);
    key_canonical = r.bytes(); source = r.str(); namespace_ = r.str(); arch = r.str();
    return a;
  } catch (const CursorDead&) { return Result<DistAuthority>(ErrorCode::MalformedFrame, "truncated build request"); }
}

std::vector<std::uint8_t> encode_build_response(RequestId rid, ArtifactId id, ArtifactGeneration gen, std::uint32_t error_code, std::string error) {
  std::vector<std::uint8_t> out;
  append_u64_be(out, rid.hi()); append_u64_be(out, rid.lo());
  append_u64_be(out, id.hi()); append_u64_be(out, id.lo());
  append_u64_be(out, gen.value);
  out.push_back(static_cast<std::uint8_t>(error_code & 0xff));
  append_u64_be(out, static_cast<std::uint64_t>(error.size())); out.insert(out.end(), error.begin(), error.end());
  return out;
}
Result<void> decode_build_response(const std::vector<std::uint8_t>& p, RequestId& rid, ArtifactId& id, ArtifactGeneration& gen, std::uint32_t& error_code, std::string& error) {
  try {
    Reader r{p.data(), p.size(), 0};
    std::uint64_t rd0 = r.u64(); std::uint64_t rd1 = r.u64(); rid = RequestId(rd0, rd1);
    std::uint64_t id0 = r.u64(); std::uint64_t id1 = r.u64(); id = ArtifactId(id0, id1);
    gen.value = r.u64();
    error_code = r.u8();
    error = r.str();
    return Result<void>();
  } catch (const CursorDead&) { return Result<void>(ErrorCode::MalformedFrame, "truncated build response"); }
}

std::vector<std::uint8_t> encode_register(WorkerId w, WorkerBootId boot, CacheGeneration gen) {
  std::vector<std::uint8_t> out;
  append_u64_be(out, w.value); append_u64_be(out, boot.value.hi()); append_u64_be(out, boot.value.lo());
  append_u64_be(out, gen.value);
  return out;
}
Result<std::pair<WorkerId, WorkerBootId>> decode_register(const std::vector<std::uint8_t>& p, CacheGeneration& gen) {
  try {
    Reader r{p.data(), p.size(), 0};
    WorkerId w(r.u64());
    std::uint64_t b0 = r.u64(); std::uint64_t b1 = r.u64(); WorkerBootId boot{StrongId128(b0, b1)};
    gen.value = r.u64();
    return std::make_pair(w, boot);
  } catch (const CursorDead&) { return Result<std::pair<WorkerId, WorkerBootId>>(ErrorCode::MalformedFrame, "truncated register"); }
}
std::vector<std::uint8_t> encode_register_ack(CoordinatorEpoch epoch, WorkerId w, std::uint8_t ok) {
  std::vector<std::uint8_t> out;
  append_u64_be(out, epoch.value); append_u64_be(out, w.value); out.push_back(ok);
  return out;
}
Result<void> decode_register_ack(const std::vector<std::uint8_t>& p, CoordinatorEpoch& epoch, WorkerId& w, std::uint8_t& ok) {
  try {
    Reader r{p.data(), p.size(), 0};
    epoch.value = r.u64(); w.value = r.u64(); ok = r.u8();
    return Result<void>();
  } catch (const CursorDead&) { return Result<void>(ErrorCode::MalformedFrame, "truncated register ack"); }
}

std::vector<std::uint8_t> encode_roll_epoch(CoordinatorEpoch epoch) {
  std::vector<std::uint8_t> out; append_u64_be(out, epoch.value); return out;
}
Result<void> decode_roll_epoch(const std::vector<std::uint8_t>& p, CoordinatorEpoch& epoch) {
  try {
    Reader r{p.data(), p.size(), 0};
    epoch.value = r.u64();
    return Result<void>();
  } catch (const CursorDead&) { return Result<void>(ErrorCode::MalformedFrame, "truncated roll-epoch"); }
}


std::vector<std::uint8_t> encode_query_workers() {
  std::vector<std::uint8_t> out;
  out.push_back('Q'); out.push_back('W');
  return out;
}
Result<std::vector<std::uint64_t>> decode_query_workers(const std::vector<std::uint8_t>& p) {
  if (p.size() < 2 || p[0] != 'Q' || p[1] != 'W') return Result<std::vector<std::uint64_t>>(ErrorCode::MalformedFrame, "bad query marker");
  return std::vector<std::uint64_t>{};
}
std::vector<std::uint8_t> encode_query_workers_resp(const std::vector<std::uint64_t>& boot_his) {
  std::vector<std::uint8_t> out;
  out.push_back(0x51u); out.push_back(0x52u);
  for (auto h : boot_his) append_u64_be(out, h);
  return out;
}
Result<std::vector<std::uint64_t>> decode_query_workers_resp(const std::vector<std::uint8_t>& p) {
  std::vector<std::uint64_t> out;
  try {
    Reader r{p.data(), p.size(), 0};
    if (r.u8() != 0x51u || r.u8() != 0x52u) return Result<std::vector<std::uint64_t>>(ErrorCode::MalformedFrame, "bad resp marker");
    while (r.i < r.n) out.push_back(r.u64());
  } catch (const CursorDead&) { return Result<std::vector<std::uint64_t>>(ErrorCode::MalformedFrame, "truncated resp"); }
  return out;
}
}  // namespace kernelcache