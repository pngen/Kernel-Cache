#include "kernelcache/backend.hpp"

namespace kernelcache {

#ifndef KC_HAS_CUDA
std::shared_ptr<KernelBackend> make_cuda_backend() { return nullptr; }
bool cuda_backend_available() { return false; }
#endif

std::string builtin_backend_names() {
  std::string s = "cpu-synth";
  if (cuda_backend_available()) s += ",cuda";
  return s;
}

}  // namespace kernelcache
