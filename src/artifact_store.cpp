// artifact_store.cpp - canonical-state / secondary-index consistency.
#include "kernelcache/key.hpp"
#include "src/internal.hpp"
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>

namespace kernelcache {

bool check_index_consistency(
    const std::unordered_map<ArtifactId, std::shared_ptr<internal::ArtifactRecord>>& by_id,
    const std::unordered_map<KernelCompatibilityKey, std::vector<ArtifactId>>& by_key,
    const std::unordered_map<std::string, std::vector<ArtifactId>>& by_operation,
    std::string* detail) {
  bool ok = true;
  std::string err;
  // Every by_key entry must reference a live artifact and be present under the
  // artifact's own key; every key entry must appear exactly once.
  for (auto& [key, ids] : by_key) {
    for (auto& id : ids) {
      auto it = by_id.find(id);
      if (it == by_id.end()) { if (err.empty()) err = "key index references unknown artifact " + id.str(); ok = false; continue; }
      if (!(it->second->key == key)) { if (err.empty()) err = "key index entry disagrees with artifact key for " + id.str(); ok = false; }
      std::size_t cnt = static_cast<std::size_t>(std::count(ids.begin(), ids.end(), id));
      if (cnt != 1) { if (err.empty()) err = "duplicate key-index entry for " + id.str(); ok = false; }
    }
  }
  // Every by_operation entry must reference a live artifact with matching op.
  for (auto& [op, ids] : by_operation) {
    for (auto& id : ids) {
      auto it = by_id.find(id);
      if (it == by_id.end()) { if (err.empty()) err = "op index references unknown artifact " + id.str(); ok = false; continue; }
      if (it->second->operation != op) { if (err.empty()) err = "op index disagrees for " + id.str(); ok = false; }
    }
  }
  // Every live artifact must appear in the key index.
  for (auto& [id, rec] : by_id) {
    bool found = false;
    for (auto& [key, ids] : by_key) { if (key == rec->key && std::find(ids.begin(), ids.end(), id) != ids.end()) { found = true; break; } }
    if (!found) { if (err.empty()) err = "artifact " + id.str() + " missing from key index"; ok = false; }
  }
  if (!ok && detail) *detail = err;
  return ok;
}

}  // namespace kernelcache
