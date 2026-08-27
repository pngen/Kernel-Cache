#include "kernelcache/key.hpp"

#include <algorithm>
#include <sstream>

#include "kernelcache/canonical.hpp"

namespace kernelcache {

namespace {
// Format-version octet for the canonical key encoding. Bumping this changes the
// digest for the same semantic inputs, which is correct because a new key-format
// changes the meaning of the encoded bytes.
constexpr std::uint8_t kKeyFormatVersion = 1;

void sort_pairs(std::vector<std::pair<std::string, std::string>>& v) {
  std::sort(v.begin(), v.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
}
void sort_strings(std::vector<std::string>& v) { std::sort(v.begin(), v.end()); }

std::string join(const std::vector<std::string>& v, const char* sep) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i) out += sep;
    out += v[i];
  }
  return out;
}
}  // namespace

KernelCompatibilityKey::KernelCompatibilityKey() = default;

bool KernelCompatibilityKey::finalize() noexcept {
  // Always recompute from the current fields. A key copied from a finalized key
  // and then mutated must re-derive its digest, so we cannot early-return here.
  finalized_ = false;
  // Canonicalize ordering so that semantically equal inputs are byte-identical.
  sort_strings(codegen_flags);
  sort_pairs(specialization);

  // Structural validation.
  if (dtypes.size() != layouts.size()) return false;
  if (!layouts.empty() && layouts.size() != rank && rank != 0) return false;
  if (!shape.empty() && shape.size() != rank && rank != 0) return false;
  if (!scalar_param_types.empty() && scalar_param_types.size() != rank && rank != 0) return false;

  canonical_.clear();
  CanonicalWriter w;
  w.tag(0x01).u8(kKeyFormatVersion);

  w.tag(0x02).str(operation);
  w.tag(0x03).str(artifact_format);

  w.tag(0x04).u8(static_cast<std::uint8_t>(vendor));
  w.tag(0x05).u8(static_cast<std::uint8_t>(family));
  w.tag(0x06).str(arch);
  w.tag(0x07).u8(capability.major).u8(capability.minor);
  w.tag(0x08).u64(device_id.hi()).u64(device_id.lo());

  w.tag(0x09).str(runtime_backend).u32(runtime_version_major).u32(runtime_version_minor);

  w.tag(0x0a).str(compiler_name)
      .u32(compiler_version_major).u32(compiler_version_minor).u32(compiler_version_patch)
      .str(compiler_backend);

  w.tag(0x0b).str(abi_name).u64(abi_version);
  w.tag(0x0c).str(kernel_interface_signature);

  w.tag(0x0d).u32(static_cast<std::uint32_t>(scalar_param_types.size()));
  for (auto& s : scalar_param_types) w.str(s);

  w.tag(0x0e).u32(static_cast<std::uint32_t>(dtypes.size()));
  for (auto d : dtypes) w.u8(static_cast<std::uint8_t>(d));

  w.tag(0x0f).u32(static_cast<std::uint32_t>(layouts.size()));
  for (auto l : layouts) w.u8(static_cast<std::uint8_t>(l));

  w.tag(0x10).u32(rank);

  w.tag(0x11).u32(static_cast<std::uint32_t>(shape.size()));
  for (auto s : shape) w.i64(s);

  w.tag(0x12).u32(static_cast<std::uint32_t>(symbolic_shape_constraints.size()));
  for (auto& s : symbolic_shape_constraints) w.str(s);

  w.tag(0x13).u32(alignment_bytes);
  w.tag(0x14).u32(shared_mem_bytes);
  w.tag(0x15).str(launch_abi);
  w.tag(0x16).u32(block_x).u32(block_y).u32(block_z);

  w.tag(0x17).u32(static_cast<std::uint32_t>(codegen_flags.size()));
  for (auto& s : codegen_flags) w.str(s);

  w.tag(0x18).u32(static_cast<std::uint32_t>(specialization.size()));
  for (auto& p : specialization) w.str(p.first).str(p.second);

  w.tag(0x19).str(model_revision);
  w.tag(0x1a).str(operator_revision);
  w.tag(0x1b).str(quantization_kind).u32(quantization_bits).boolean(quantization_symmetric);

  canonical_ = w.buffer();
  digest_ = Sha256::digest(std::span<const std::uint8_t>(canonical_.data(), canonical_.size()));
  digest_hex_ = to_hex(digest_);

  std::ostringstream os;
  os << "op=" << (operation.empty() ? "<none>" : operation)
     << " fmt=" << (artifact_format.empty() ? "<none>" : artifact_format)
     << " vendor=" << vendor_name(vendor)
     << " family=" << static_cast<int>(family)
     << " arch=" << (arch.empty() ? "<none>" : arch)
     << " cc=" << static_cast<int>(capability.major) << "." << static_cast<int>(capability.minor)
     << " rt=" << runtime_backend
     << " comp=" << compiler_name << ":" << compiler_version_major << "." << compiler_version_minor << "." << compiler_version_patch
     << " abi=" << abi_name << "@" << abi_version
     << " sig=" << kernel_interface_signature
     << " dt=[";
  for (std::size_t i = 0; i < dtypes.size(); ++i) {
    if (i) os << ",";
    os << datatype_name(dtypes[i]);
  }
  os << "] rank=" << rank << " shape=[";
  for (std::size_t i = 0; i < shape.size(); ++i) {
    if (i) os << ",";
    os << shape[i];
  }
  os << "] align=" << alignment_bytes << " smem=" << shared_mem_bytes
     << " block=" << block_x << "x" << block_y << "x" << block_z
     << " sha256=" << digest_hex_;
  canonical_text_ = os.str();

  finalized_ = true;
  return true;
}

Result<void> KernelCompatibilityKey::validate() const noexcept {
  if (rank != 0) {
    if (!layouts.empty() && layouts.size() != rank)
      return Result<void>(ErrorCode::InvalidArgument, "layouts.size() != rank");
    if (!shape.empty() && shape.size() != rank)
      return Result<void>(ErrorCode::InvalidArgument, "shape.size() != rank");
    if (!scalar_param_types.empty() && scalar_param_types.size() != rank)
      return Result<void>(ErrorCode::InvalidArgument, "scalar_param_types.size() != rank");
  }
  if (!finalized_) {
    KernelCompatibilityKey* self = const_cast<KernelCompatibilityKey*>(this);
    if (!self->finalize())
      return Result<void>(ErrorCode::InvalidArgument, "finalize() rejected inconsistent key");
  }
  return Result<void>();
}

bool KernelCompatibilityKey::operator==(const KernelCompatibilityKey& o) const noexcept {
  const KernelCompatibilityKey* a = this;
  const KernelCompatibilityKey* b = &o;
  if (!a->finalized_) const_cast<KernelCompatibilityKey*>(a)->finalize();
  if (!b->finalized_) const_cast<KernelCompatibilityKey*>(b)->finalize();
  return a->digest_ == b->digest_;
}

std::size_t KernelCompatibilityKey::hash() const noexcept {
  if (!finalized_) const_cast<KernelCompatibilityKey*>(this)->finalize();
  std::size_t h = 0xcbf29ce484222325ULL;
  for (auto b : digest_) {
    h ^= static_cast<std::size_t>(b);
    h *= 0x100000001b3ULL;
    h &= std::size_t(-1);
  }
  return h;
}

std::string key_summary(const KernelCompatibilityKey& k) {
  std::string s = k.canonical_text();
  return s;
}

// ---------------------------------------------------------------------------
// Canonical decoder
// ---------------------------------------------------------------------------
namespace {
struct CursorDead {};
struct Reader {
  const std::uint8_t* p = nullptr;
  std::size_t n = 0;
  std::size_t i = 0;

  bool nbytes(std::size_t k) const { return i + k <= n; }
  std::uint8_t u8() {
    if (!nbytes(1)) throw CursorDead{};
    return p[i++];
  }
  std::uint16_t u16() {
    std::uint16_t v = static_cast<std::uint16_t>(u8()) << 8; v |= u8(); return v;
  }
  std::uint32_t u32() {
    std::uint32_t v = 0; for (int k = 3; k >= 0; --k) v = (v << 8) | u8(); return v;
  }
  std::uint64_t u64() {
    std::uint64_t v = 0; for (int k = 7; k >= 0; --k) v = (v << 8) | u8(); return v;
  }
  std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
  bool boolean() { return u8() != 0; }
  std::string str() {
    std::uint64_t len = u64();
    if (len > n - i) throw CursorDead{};
    std::string s(reinterpret_cast<const char*>(p + i), static_cast<std::size_t>(len));
    i += static_cast<std::size_t>(len);
    return s;
  }
  std::uint32_t count() { return u32(); }
};
}  // namespace

Result<KernelCompatibilityKey> KernelCompatibilityKey::from_canonical(
    std::span<const std::uint8_t> bytes) noexcept {
  KernelCompatibilityKey k;
  try {
    Reader r{bytes.data(), bytes.size(), 0};
    for (;;) {
      if (r.i >= r.n) break;
      std::uint8_t tag = r.u8();
      switch (tag) {
        case 0x01: { (void)r.u8(); break; }  // key-format version
        case 0x02: k.operation = r.str(); break;
        case 0x03: k.artifact_format = r.str(); break;
        case 0x04: k.vendor = static_cast<DeviceVendor>(r.u8()); break;
        case 0x05: k.family = static_cast<AcceleratorFamily>(r.u8()); break;
        case 0x06: k.arch = r.str(); break;
        case 0x07: k.capability.major = r.u8(); k.capability.minor = r.u8(); break;
        case 0x08: { std::uint64_t d0 = r.u64(); std::uint64_t d1 = r.u64(); k.device_id = DeviceId(d0, d1); } break;
        case 0x09: k.runtime_backend = r.str(); k.runtime_version_major = r.u32(); k.runtime_version_minor = r.u32(); break;
        case 0x0a: k.compiler_name = r.str(); k.compiler_version_major = r.u32();
                   k.compiler_version_minor = r.u32(); k.compiler_version_patch = r.u32();
                   k.compiler_backend = r.str(); break;
        case 0x0b: k.abi_name = r.str(); k.abi_version = r.u64(); break;
        case 0x0c: k.kernel_interface_signature = r.str(); break;
        case 0x0d: { std::uint32_t c = r.count(); k.scalar_param_types.clear();
                     for (std::uint32_t j = 0; j < c; ++j) k.scalar_param_types.push_back(r.str()); break; }
        case 0x0e: { std::uint32_t c = r.count(); k.dtypes.clear();
                     for (std::uint32_t j = 0; j < c; ++j) k.dtypes.push_back(static_cast<Datatype>(r.u8())); break; }
        case 0x0f: { std::uint32_t c = r.count(); k.layouts.clear();
                     for (std::uint32_t j = 0; j < c; ++j) k.layouts.push_back(static_cast<TensorLayout>(r.u8())); break; }
        case 0x10: k.rank = r.u32(); break;
        case 0x11: { std::uint32_t c = r.count(); k.shape.clear();
                     for (std::uint32_t j = 0; j < c; ++j) k.shape.push_back(r.i64()); break; }
        case 0x12: { std::uint32_t c = r.count(); k.symbolic_shape_constraints.clear();
                     for (std::uint32_t j = 0; j < c; ++j) k.symbolic_shape_constraints.push_back(r.str()); break; }
        case 0x13: k.alignment_bytes = r.u32(); break;
        case 0x14: k.shared_mem_bytes = r.u32(); break;
        case 0x15: k.launch_abi = r.str(); break;
        case 0x16: k.block_x = r.u32(); k.block_y = r.u32(); k.block_z = r.u32(); break;
        case 0x17: { std::uint32_t c = r.count(); k.codegen_flags.clear();
                     for (std::uint32_t j = 0; j < c; ++j) k.codegen_flags.push_back(r.str()); break; }
        case 0x18: { std::uint32_t c = r.count(); k.specialization.clear();
                     for (std::uint32_t j = 0; j < c; ++j) {
                       std::string key = r.str(); std::string val = r.str();
                       k.specialization.emplace_back(std::move(key), std::move(val));
                     } break; }
        case 0x19: k.model_revision = r.str(); break;
        case 0x1a: k.operator_revision = r.str(); break;
        case 0x1b: k.quantization_kind = r.str(); k.quantization_bits = r.u32();
                   k.quantization_symmetric = r.boolean(); break;
        default: return Result<KernelCompatibilityKey>(ErrorCode::MalformedFrame, "unknown canonical key tag");
      }
    }
    // Re-derive digest from the decoded fields so the digest agrees with content.
    if (!k.finalize()) return Result<KernelCompatibilityKey>(ErrorCode::InvalidArtifact, "reconstructed key rejected");
  } catch (const CursorDead&) {
    return Result<KernelCompatibilityKey>(ErrorCode::MalformedFrame, "truncated canonical key");
  }
  return k;
}

}  // namespace kernelcache