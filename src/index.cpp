// index.cpp - secondary index helpers.
#include "kernelcache/key.hpp"
#include <unordered_map>
#include <vector>
#include <string>

namespace kernelcache {

// Total number of canonical key entries in the key index.
std::size_t key_index_members(
    const std::unordered_map<KernelCompatibilityKey, std::vector<ArtifactId>>& by_key) {
  std::size_t n = 0;
  for (auto& [k, v] : by_key) n += v.size();
  return n;
}

// Exact key lookup in the index.
std::vector<ArtifactId> query_by_key(
    const std::unordered_map<KernelCompatibilityKey, std::vector<ArtifactId>>& by_key,
    const KernelCompatibilityKey& k) {
  auto it = by_key.find(k);
  return it == by_key.end() ? std::vector<ArtifactId>{} : it->second;
}

}  // namespace kernelcache
