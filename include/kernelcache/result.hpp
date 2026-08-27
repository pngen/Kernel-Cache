// result.hpp - typed error result for KernelCache.
#pragma once

#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <optional>

namespace kernelcache {

// ---------------------------------------------------------------------------
// ErrorCode
// ---------------------------------------------------------------------------
enum class ErrorCode : std::uint32_t {
  None = 0,

  // Compatibility / correctness
  IncompatibleArchitecture = 1,
  IncompatibleRuntime = 2,
  IncompatibleCompilerABI = 3,
  IncompatibleKernelABI = 4,
  IncompatibleDatatype = 5,
  IncompatibleLayout = 6,
  IncompatibleShape = 7,
  IncompatibleAlignment = 8,
  IncompatibleSpecialization = 9,
  IncompatibleQuantization = 10,
  InvalidArtifact = 11,
  StaleArtifact = 12,
  CorruptArtifact = 13,
  PolicyRejected = 14,

  // Lifecycle / state
  BadStateTransition = 20,
  InvalidArgument = 21,
  NotFound = 22,
  AlreadyExists = 23,
  NotSupported = 24,
  BuildFailed = 25,
  ValidationFailed = 26,
  LoadFailed = 27,
  UnloadFailed = 28,

  // Generation authority
  StaleGeneration = 30,
  ObsoleteBuildAttempt = 31,
  ObsoleteArtifact = 32,
  EpochMismatch = 33,
  WorkerBootMismatch = 34,
  DuplicateBuildCompletion = 35,

  // Residency
  EvictionForbidden = 40,
  ResidencyExceeded = 41,
  NotResident = 42,

  // Persistence / I/O
  IoError = 50,
  ChecksumMismatch = 51,
  TruncatedData = 52,
  UnknownMetadataVersion = 53,
  PartialWrite = 54,
  OrphanTemp = 55,

  // Protocol
  ProtocolError = 60,
  FrameTooLarge = 61,
  MalformedFrame = 62,
  UnknownMessageType = 63,
  ProtocolVersionMismatch = 64,

  // System
  OutOfMemory = 70,
  ThreadError = 71,
  Cancelled = 72,
  Timeout = 73,
  ConcurrencyConflict = 74,
  InternalError = 75,
};

// Human-readable name for an error code.
const char* error_code_name(ErrorCode code) noexcept;

// Human-readable, stable short text (used in reports, not for identity).
std::string to_string(ErrorCode code);

// ---------------------------------------------------------------------------
// KcError
// ---------------------------------------------------------------------------
class KcError {
 public:
  KcError() = default;
  KcError(ErrorCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  ErrorCode code() const noexcept { return code_; }
  const std::string& message() const noexcept { return message_; }
  std::string to_string() const;

  explicit operator bool() const noexcept { return code_ != ErrorCode::None; }

 private:
  ErrorCode code_ = ErrorCode::None;
  std::string message_;
};

// ---------------------------------------------------------------------------
// ok() tag
// ---------------------------------------------------------------------------
struct OkTag {};
inline constexpr OkTag kOk{};

// ---------------------------------------------------------------------------
// Result<T> / Result<void>
// ---------------------------------------------------------------------------
template <typename T>
class Result {
 public:
  Result() = default;
  Result(T value) : storage_(std::in_place_index<0>, std::move(value)) {}
  Result(KcError error) : storage_(std::in_place_index<1>, std::move(error)) {}
  Result(ErrorCode code) : storage_(std::in_place_index<1>, KcError(code, error_code_name(code))) {}
  Result(ErrorCode code, std::string message)
      : storage_(std::in_place_index<1>, KcError(code, std::move(message))) {}

  bool ok() const noexcept { return storage_.index() == 0; }
  bool has_error() const noexcept { return !ok(); }
  explicit operator bool() const noexcept { return ok(); }

  T& value() & { return std::get<0>(storage_); }
  const T& value() const& { return std::get<0>(storage_); }
  T&& value() && { return std::move(std::get<0>(storage_)); }

  const T& operator*() const& { return std::get<0>(storage_); }
  T& operator*() & { return std::get<0>(storage_); }

  KcError& error() & { return std::get<1>(storage_); }
  const KcError& error() const& { return std::get<1>(storage_); }

  template <typename U>
  T value_or(U&& fallback) const& { return ok() ? value() : static_cast<T>(std::forward<U>(fallback)); }
  T value_or(T fallback) const { return ok() ? value() : fallback; }

 private:
  std::variant<T, KcError> storage_;
};

template <>
class Result<void> {
 public:
  Result() = default;
  Result(OkTag) {}  // NOLINT
  Result(KcError error) : error_(std::move(error)) {}
  Result(ErrorCode code) : error_(code, error_code_name(code)) {}
  Result(ErrorCode code, std::string message) : error_(code, std::move(message)) {}

  bool ok() const noexcept { return !error_; }
  bool has_error() const noexcept { return static_cast<bool>(error_); }
  explicit operator bool() const noexcept { return ok(); }
  KcError& error() & { return error_; }
  const KcError& error() const& { return error_; }

 private:
  KcError error_;
};

inline Result<void> ok() { return Result<void>(); }

// Convenience helper builders.
inline Result<void> make_ok() { return Result<void>(); }
inline KcError make_error(ErrorCode code, std::string message) {
  return KcError(code, std::move(message));
}

}  // namespace kernelcache
