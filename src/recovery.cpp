#include "kernelcache/kernel_cache.hpp"
#include "src/impl.hpp"
#include "src/internal.hpp"
#include "kernelcache/lifecycle.hpp"

namespace kernelcache {

using internal::ArtifactRecord;

Result<std::size_t> KernelCache::Impl::recover_impl(std::vector<std::string>* rejected,
                                                    std::vector<std::string>* orphans) {
  if (!persistence_open || !persistence) return std::size_t(0);
  std::vector<std::string> local_rejected, local_orphans;
  if (!rejected) rejected = &local_rejected;
  if (!orphans) orphans = &local_orphans;
  auto res = persistence->recover(rejected, orphans);
  if (!res) return Result<std::size_t>(res.error().code(), res.error().message());
  std::size_t count = 0;
  {
    std::lock_guard<std::shared_mutex> lk(mtx_);
    for (auto& sa : res.value()) {
      if (by_id.count(sa.id)) { continue; }
      ArtifactDescriptor desc;
      desc.id = sa.id;
      desc.generation = sa.generation;
      desc.key = sa.key;
      desc.format = sa.format;
      desc.namespace_ = sa.namespace_;
      desc.backend = sa.provenance.producer;
      desc.sizes = sa.sizes;
      desc.provenance = sa.provenance;
      desc.bytes_sha256 = sa.sha256_hex.value_or("");
      desc.published_at_ns = sa.stored_ns;
      desc.validation = ValidationDescriptor{};
      desc.validation.integrity_hash_checked = true;
      desc.validation.format_checked = true;
      desc.validation.metadata_consistent = true;
      // Device residency is presumed absent on a fresh process.
      auto rec = make_record(desc, sa.bytes);
      rec->persistent = true;
      rec->tier = ResidencyTier::PersistentStorage;
      rec->state = ArtifactState::Persisted;
      rec->validated = true;
      rec->tenant = sa.namespace_;
      rec->operation = sa.key.operation;
      rec->arch = sa.key.arch;
      insert_index_locked(rec);
      ++count;
      bump([&](Stats& s){ ++s.persistent_reloads; });
    }
  }
  record_event("recover", "restored=" + std::to_string(count) + " rejected=" + std::to_string(rejected->size()));
  return count;
}

}  // namespace kernelcache
