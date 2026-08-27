#include "kernelcache/result.hpp"
#include "kernelcache/lookup.hpp"
#include "kernelcache/invalidation.hpp"

namespace kernelcache {

const char* error_code_name(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::None: return "none";
    case ErrorCode::IncompatibleArchitecture: return "incompatible-architecture";
    case ErrorCode::IncompatibleRuntime: return "incompatible-runtime";
    case ErrorCode::IncompatibleCompilerABI: return "incompatible-compiler-abi";
    case ErrorCode::IncompatibleKernelABI: return "incompatible-kernel-abi";
    case ErrorCode::IncompatibleDatatype: return "incompatible-datatype";
    case ErrorCode::IncompatibleLayout: return "incompatible-layout";
    case ErrorCode::IncompatibleShape: return "incompatible-shape";
    case ErrorCode::IncompatibleAlignment: return "incompatible-alignment";
    case ErrorCode::IncompatibleSpecialization: return "incompatible-specialization";
    case ErrorCode::IncompatibleQuantization: return "incompatible-quantization";
    case ErrorCode::InvalidArtifact: return "invalid-artifact";
    case ErrorCode::StaleArtifact: return "stale-artifact";
    case ErrorCode::CorruptArtifact: return "corrupt-artifact";
    case ErrorCode::PolicyRejected: return "policy-rejected";
    case ErrorCode::BadStateTransition: return "bad-state-transition";
    case ErrorCode::InvalidArgument: return "invalid-argument";
    case ErrorCode::NotFound: return "not-found";
    case ErrorCode::AlreadyExists: return "already-exists";
    case ErrorCode::NotSupported: return "not-supported";
    case ErrorCode::BuildFailed: return "build-failed";
    case ErrorCode::ValidationFailed: return "validation-failed";
    case ErrorCode::LoadFailed: return "load-failed";
    case ErrorCode::UnloadFailed: return "unload-failed";
    case ErrorCode::StaleGeneration: return "stale-generation";
    case ErrorCode::ObsoleteBuildAttempt: return "obsolete-build-attempt";
    case ErrorCode::ObsoleteArtifact: return "obsolete-artifact";
    case ErrorCode::EpochMismatch: return "epoch-mismatch";
    case ErrorCode::WorkerBootMismatch: return "worker-boot-mismatch";
    case ErrorCode::DuplicateBuildCompletion: return "duplicate-build-completion";
    case ErrorCode::EvictionForbidden: return "eviction-forbidden";
    case ErrorCode::ResidencyExceeded: return "residency-exceeded";
    case ErrorCode::NotResident: return "not-resident";
    case ErrorCode::IoError: return "io-error";
    case ErrorCode::ChecksumMismatch: return "checksum-mismatch";
    case ErrorCode::TruncatedData: return "truncated-data";
    case ErrorCode::UnknownMetadataVersion: return "unknown-metadata-version";
    case ErrorCode::PartialWrite: return "partial-write";
    case ErrorCode::OrphanTemp: return "orphan-temp";
    case ErrorCode::ProtocolError: return "protocol-error";
    case ErrorCode::FrameTooLarge: return "frame-too-large";
    case ErrorCode::MalformedFrame: return "malformed-frame";
    case ErrorCode::UnknownMessageType: return "unknown-message-type";
    case ErrorCode::ProtocolVersionMismatch: return "protocol-version-mismatch";
    case ErrorCode::OutOfMemory: return "out-of-memory";
    case ErrorCode::ThreadError: return "thread-error";
    case ErrorCode::Cancelled: return "cancelled";
    case ErrorCode::Timeout: return "timeout";
    case ErrorCode::ConcurrencyConflict: return "concurrency-conflict";
    case ErrorCode::InternalError: return "internal-error";
  }
  return "unknown";
}

std::string to_string(ErrorCode code) { return error_code_name(code); }

std::string KcError::to_string() const {
  std::string s = error_code_name(code_);
  if (!message_.empty()) { s += ": "; s += message_; }
  return s;
}

const char* lookup_outcome_name(LookupOutcome o) noexcept {
  switch (o) {
    case LookupOutcome::ExactHit: return "exact-hit";
    case LookupOutcome::CompatibleHit: return "compatible-hit";
    case LookupOutcome::HostResidentHit: return "host-resident-hit";
    case LookupOutcome::DeviceResidentHit: return "device-resident-hit";
    case LookupOutcome::PersistedHit: return "persisted-hit";
    case LookupOutcome::MissRequiresBuild: return "miss-requires-build";
    case LookupOutcome::MissIncompatibility: return "miss-incompatibility";
    case LookupOutcome::MissInvalidated: return "miss-invalidated";
    case LookupOutcome::MissCorrupt: return "miss-corrupt";
    case LookupOutcome::MissResidencyPressure: return "miss-residency-pressure";
    case LookupOutcome::MissPolicy: return "miss-policy";
  }
  return "unknown";
}

std::string to_string(LookupOutcome o) { return lookup_outcome_name(o); }

const char* invalidation_target_name(InvalidationTarget t) noexcept {
  switch (t) {
    case InvalidationTarget::ArtifactId: return "artifact-id";
    case InvalidationTarget::KernelId: return "kernel-id";
    case InvalidationTarget::CompatibilityKey: return "compatibility-key";
    case InvalidationTarget::Operation: return "operation";
    case InvalidationTarget::CompilerGeneration: return "compiler-generation";
    case InvalidationTarget::RuntimeGeneration: return "runtime-generation";
    case InvalidationTarget::Architecture: return "architecture";
    case InvalidationTarget::ModelRevision: return "model-revision";
    case InvalidationTarget::OperatorRevision: return "operator-revision";
    case InvalidationTarget::Namespace: return "namespace";
    case InvalidationTarget::All: return "all";
  }
  return "unknown";
}

}  // namespace kernelcache
