#include "kernelcache/kernel_cache.hpp"
#include "src/impl.hpp"
#include "src/internal.hpp"
#include "kernelcache/sha256.hpp"
#include <chrono>
#include "kernelcache/canonical.hpp"

namespace kernelcache {

using internal::BuildFence;

namespace {
std::uint64_t now_ns() {
  auto t = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t).count());
}
ArtifactId derive_id(const std::vector<std::uint8_t>& bytes, ArtifactGeneration gen) {
  Sha256Digest d = Sha256::digest(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
  std::uint64_t lo = 0; for (int i = 0; i < 8; ++i) lo = (lo << 8) | d[i];
  return ArtifactId(gen.value, lo);
}
}  // namespace

// ---------------------------------------------------------------------------
// run_build: real backend build + validation. Runs with NO cache lock held.
// ---------------------------------------------------------------------------
internal::BuildFence::Outcome KernelCache::Impl::run_build(KernelLookupRequest request,
                                                           ArtifactGeneration target_gen,
                                                           BuildAttemptId attempt) {
  internal::BuildFence::Outcome o;
  request.key.finalize();
  auto t0 = now_ns();
  auto backend = find_backend(request.key);
  if (!backend || !backend->builder || !backend->validator || !backend->loader) {
    o.succeeded = false;
    o.error_code = ErrorCode::NotSupported;
    o.error = "no complete backend (builder/validator/loader) for backend=" + request.key.runtime_backend;
    o.id = derive_id(std::vector<std::uint8_t>{}, target_gen);
    o.generation = target_gen;
    return o;
  }
  BuildRequest br;
  br.key = request.key;
  br.source = request.source;
  br.backend = backend->name;
  br.device = request.device;
  br.namespace_ = request.namespace_.empty() ? cfg.namespace_ : request.namespace_;
  br.logical_operation = request.key.operation;
  // resolve the device architecture into the compile descriptor.
  br.compile.target.vendor = br.device.vendor;
  br.compile.target.family = br.device.family;
  br.compile.target.arch_name = br.device.arch_name;
  br.compile.target.compute_capability = br.device.capability;
  br.compile.compiler.name = request.key.compiler_name.empty() ? backend->name : request.key.compiler_name;
  br.compile.compiler.version_major = request.key.compiler_version_major;
  br.compile.compiler.version_minor = request.key.compiler_version_minor;
  br.compile.compiler.version_patch = request.key.compiler_version_patch;
  br.compile.codegen_flags.flags = request.key.codegen_flags;

  auto bres = backend->builder->build(br);
  if (!bres) {
    o.succeeded = false; o.error_code = bres.error().code(); o.error = bres.error().message();
    o.id = derive_id(std::vector<std::uint8_t>{}, target_gen); o.generation = target_gen;
    return o;
  }
  auto build_result = std::move(bres.value());
  build_result.id = derive_id(build_result.bytes, target_gen);
  build_result.generation = target_gen;
  std::uint64_t build_ns = now_ns() - t0;

  // Validation stage (real, backend-specific).
  auto vres = backend->validator->validate(request.key, build_result.bytes, request.device);
  std::uint64_t val_ns = now_ns() - t0 - build_ns;
  if (!vres && vres.error().code() != ErrorCode::NotSupported) {
    o.succeeded = false; o.error_code = vres.error().code(); o.error = "validation: " + vres.error().message();
    o.id = build_result.id; o.generation = target_gen;
    return o;
  }
  ValidationDescriptor vd = vres ? vres.value() : ValidationDescriptor{};
  if (!vd.all_passed()) {
    o.succeeded = false; o.error_code = ErrorCode::ValidationFailed;
    o.error = "execution validation failed: " + (vd.backend_reason.empty() ? std::string("unspecified") : vd.backend_reason);
    o.id = build_result.id; o.generation = target_gen;
    return o;
  }

  // Build the immutable descriptor.
  ArtifactDescriptor desc;
  desc.id = build_result.id;
  desc.generation = target_gen;
  desc.key = request.key;
  desc.format = build_result.artifact_format;
  desc.namespace_ = br.namespace_;
  desc.backend = backend->name;
  desc.sizes = build_result.sizes;
  desc.compilation = build_result.compilation;
  desc.validation = vd;
  desc.provenance = build_result.provenance;
  desc.bytes_sha256 = to_hex(Sha256::digest(std::span<const std::uint8_t>(build_result.bytes.data(), build_result.bytes.size())));
  desc.published_at_ns = now_ns();
  desc.kernel_count = build_result.kernel_count;
  desc.compilation.target.vendor = br.device.vendor;
  desc.compilation.target.family = br.device.family;
  desc.compilation.target.arch_name = br.device.arch_name;
  desc.compilation.target.compute_capability = br.device.capability;

  o.id = desc.id; o.generation = target_gen; o.desc = std::move(desc);
  o.bytes = std::move(build_result.bytes); o.succeeded = true;

  bump([&](Stats& s){ s.compile_ns += build_ns; s.validation_ns += val_ns; if (backend->name == "cuda") s.cuda_exec_validations += 1; });
  record_event("build", "attempt=" + attempt.str() + " id=" + o.id.str() + " backend=" + backend->name + " build_ms=" + std::to_string(build_ns / 1000000));
  return o;
}

}  // namespace kernelcache