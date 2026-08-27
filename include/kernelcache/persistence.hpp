// persistence.hpp - persistent artifact storage interface.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include "kernelcache/identifiers.hpp"
#include "kernelcache/result.hpp"
#include "kernelcache/descriptors.hpp"
#include "kernelcache/key.hpp"

namespace kernelcache {

// On-disk artifact payload plus validated metadata.
struct StoredArtifact {
  ArtifactId id;
  ArtifactGeneration generation;
  KernelCompatibilityKey key;       // finalized serialized key
  std::vector<std::uint8_t> bytes;
  ArtifactSizes sizes;
  ProvenanceDescriptor provenance;
  std::string format;
  std::uint64_t stored_ns = 0;
  std::optional<std::string> sha256_hex;  // checksum of bytes
  std::string namespace_;
};

// Abstract persistence store. Implementations must be crash-safe (temp write +
// atomic rename), checksum the payload and metadata, and never trust metadata
// without integrity verification.
class PersistenceStore {
 public:
  virtual ~PersistenceStore() = default;
  virtual std::string backend_name() const = 0;
  virtual Result<void> open(const std::string& root) = 0;
  virtual Result<void> put(const StoredArtifact& artifact) = 0;
  virtual Result<StoredArtifact> get(const ArtifactId& id) = 0;
  virtual Result<void> remove(const ArtifactId& id) = 0;
  virtual Result<std::vector<ArtifactId>> list() = 0;
  // Recovery: scan root, verify checksums, return valid artifacts, and report
  // rejected (corrupt/truncated) and orphan-temp artifacts.
  virtual Result<std::vector<StoredArtifact>> recover(std::vector<std::string>* rejected,
                                                       std::vector<std::string>* orphans) = 0;
  virtual Result<void> close() = 0;
};

// Concrete filesystem-backed store.
class FilePersistenceStore final : public PersistenceStore {
 public:
  std::string backend_name() const override { return "file"; }
  Result<void> open(const std::string& root) override;
  Result<void> put(const StoredArtifact& artifact) override;
  Result<StoredArtifact> get(const ArtifactId& id) override;
  Result<void> remove(const ArtifactId& id) override;
  Result<std::vector<ArtifactId>> list() override;
  Result<std::vector<StoredArtifact>> recover(std::vector<std::string>* rejected,
                                              std::vector<std::string>* orphans) override;
  Result<void> close() override;
 private:
  std::string root_;
  std::string meta_dir_, blob_dir_;
};

}  // namespace kernelcache
