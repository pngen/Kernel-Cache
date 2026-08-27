// distributed.hpp - framed, authority-carrying distributed cache protocol.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <utility>

#include "kernelcache/identifiers.hpp"
#include "kernelcache/result.hpp"

namespace kernelcache {

constexpr std::uint32_t kDistProtoVersion = 1;
constexpr std::size_t kDistMaxFrameBytes = 64u * 1024u * 1024u;  // hard cap
constexpr std::uint16_t kDistHeaderBytes = 8;  // [frame_len u32][ver u16][type u16]

// Message types carried over the framed protocol.
enum class DistMsgType : std::uint16_t {
  Hello_W = 1,          // worker -> coordinator
  Register_W = 2,       // worker -> coordinator
  RegisterAck_C = 3,    // coordinator -> worker
  LookupReq = 4,        // client -> coordinator
  LookupResp = 5,       // coordinator -> client
  BuildReq = 6,         // coordinator -> worker
  BuildResp = 7,        // worker -> coordinator
  InvalidateReq = 8,    // client -> coordinator
  InvalidateResp = 9,   // coordinator -> client
  RollEpoch = 10,       // coordinator -> all
  RollEpochAck = 11,    // worker -> coordinator
  EpochInvalid = 12,    // coordinator -> worker (epoch changed)
  Shutdown = 13,
};

const char* dist_msg_type_name(DistMsgType t) noexcept;

// Authority carried by every authoritative message.
struct DistAuthority {
  CoordinatorEpoch epoch{0};
  WorkerId worker{0};
  WorkerBootId boot;
  CacheGeneration cache_gen{0};
  ArtifactGeneration artifact_gen{0};
  BuildAttemptId attempt;
  RequestId request;
};

// --- framing -----------------------------------------------------------------
// Encode a frame: [frame_len u32][proto_ver u16][msg_type u16][payload].
// frame_len counts the whole frame including the 8-byte header.
std::vector<std::uint8_t> encode_frame(DistMsgType type, const std::vector<std::uint8_t>& payload);

// Strict decode: validates length, protocol version, known message type, and
// exact remaining size. Rejects oversized/truncated/unknown frames.
Result<std::pair<DistMsgType, std::vector<std::uint8_t>>> decode_frame(
    std::span<const std::uint8_t> frame);

// --- payload marshalling ----------------------------------------------------
// Authority marshalling (lossless 64-bit). Returns false if fields malformed.
bool encode_authority(std::vector<std::uint8_t>& out, const DistAuthority& a);
bool decode_authority(const std::vector<std::uint8_t>& payload, std::size_t& pos, DistAuthority& a);

// Lookup request/response.
std::vector<std::uint8_t> encode_lookup_request(const DistAuthority& a, RequestId rid,
                                                const std::string& operation,
                                                const std::vector<std::uint8_t>& key_canonical,
                                                const std::string& namespace_,
                                                std::uint8_t desired_tier);
Result<DistAuthority> decode_lookup_request(const std::vector<std::uint8_t>& p, RequestId& rid,
                                            std::string& operation,
                                            std::vector<std::uint8_t>& key_canonical,
                                            std::string& namespace_, std::uint8_t& desired_tier);

std::vector<std::uint8_t> encode_lookup_response(RequestId rid, std::uint8_t outcome,
                                                 ArtifactId id, ArtifactGeneration gen,
                                                 std::uint8_t compatible);
Result<void> decode_lookup_response(const std::vector<std::uint8_t>& p, RequestId& rid,
                                    std::uint8_t& outcome, ArtifactId& id,
                                    ArtifactGeneration& gen, std::uint8_t& compatible);

// Build request/response (dispatched by the coordinator to a worker).
std::vector<std::uint8_t> encode_build_request(const DistAuthority& a, RequestId rid,
                                               const std::vector<std::uint8_t>& key_canonical,
                                               const std::string& source,
                                               const std::string& namespace_,
                                               const std::string& arch);
Result<DistAuthority> decode_build_request(const std::vector<std::uint8_t>& p, RequestId& rid,
                                           std::vector<std::uint8_t>& key_canonical,
                                           std::string& source, std::string& namespace_,
                                           std::string& arch);

std::vector<std::uint8_t> encode_build_response(RequestId rid, ArtifactId id, ArtifactGeneration gen,
                                                std::uint32_t error_code, std::string error);
Result<void> decode_build_response(const std::vector<std::uint8_t>& p, RequestId& rid, ArtifactId& id,
                                   ArtifactGeneration& gen, std::uint32_t& error_code, std::string& error);

// Register handshake.
std::vector<std::uint8_t> encode_register(WorkerId w, WorkerBootId boot, CacheGeneration gen);
Result<std::pair<WorkerId, WorkerBootId>> decode_register(const std::vector<std::uint8_t>& p, CacheGeneration& gen);
std::vector<std::uint8_t> encode_register_ack(CoordinatorEpoch epoch, WorkerId w, std::uint8_t ok);
Result<void> decode_register_ack(const std::vector<std::uint8_t>& p, CoordinatorEpoch& epoch, WorkerId& w, std::uint8_t& ok);

std::vector<std::uint8_t> encode_roll_epoch(CoordinatorEpoch epoch);
Result<void> decode_roll_epoch(const std::vector<std::uint8_t>& p, CoordinatorEpoch& epoch);

}  // namespace kernelcache