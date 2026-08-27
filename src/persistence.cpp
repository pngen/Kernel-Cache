#include "kernelcache/persistence.hpp"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <random>
#include <chrono>
#include <ctime>
#include <cstring>
#include <cctype>

#include "kernelcache/sha256.hpp"
#include "kernelcache/canonical.hpp"

namespace fs = std::filesystem;

namespace kernelcache {

namespace {
constexpr const char* kFormatMagic = "KC-PERSIST-1";
constexpr const char* kMetaMagic = "KCMETA1";

const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string b64encode(const std::string& in) {
  std::string out; int val = 0, bits = -6;
  for (unsigned char c : in) { val = (val << 8) + c; bits += 8; while (bits >= 0) { out.push_back(kB64[(val >> bits) & 0x3f]); bits -= 6; } }
  if (bits > -6) out.push_back(kB64[((val << 8) >> (bits + 8)) & 0x3f]);
  while (out.size() % 4) out.push_back('=');
  return out;
}
std::string b64decode(const std::string& in) {
  std::string out; int val = 0, bits = -8;
  for (char c : in) { if (c == '=') break; const char* p = std::strchr(kB64, c); if (!p) return {}; val = (val << 6) + static_cast<int>(p - kB64); bits += 6; if (bits >= 0) { out.push_back(static_cast<char>((val >> bits) & 0xff)); bits -= 8; } }
  return out;
}

std::string sha256_hex_bytes(const std::vector<std::uint8_t>& bytes) {
  return to_hex(Sha256::digest(std::span<const std::uint8_t>(bytes.data(), bytes.size())));
}
std::string sha256_hex_str(const std::string& s) { return to_hex(Sha256::digest(s)); }

std::string read_file(const fs::path& p, std::uint64_t* len_out) {
  std::ifstream f(p, std::ios::binary); if (!f) return {};
  std::ostringstream ss; ss << f.rdbuf();
  if (len_out) *len_out = static_cast<std::uint64_t>(ss.str().size());
  return ss.str();
}
bool write_file(const fs::path& p, const std::string& data) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc); if (!f) return false;
  f.write(data.data(), static_cast<std::streamsize>(data.size())); f.flush(); return f.good();
}

std::uint64_t now_ns() {
  auto t = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t).count());
}

bool field_ok(const std::string& line, const std::string& key) { return line.rfind(key + "=", 0) == 0; }

std::string build_meta(const StoredArtifact& a) {
  std::ostringstream os;
  os << kMetaMagic << "\n";
  os << "id=" << a.id.str() << "\n";
  os << "generation=" << a.generation.value << "\n";
  os << "format=" << b64encode(a.format) << "\n";
  os << "namespace=" << b64encode(a.namespace_) << "\n";
  os << "backend=" << b64encode(a.provenance.producer) << "\n";
  os << "operation=" << b64encode(a.key.operation) << "\n";
  os << "tenant=" << b64encode(a.provenance.tenant) << "\n";
  os << "artifact_sha256=" << a.sha256_hex.value_or("") << "\n";
  os << "sizes=" << a.sizes.artifact_bytes << "," << a.sizes.host_bytes << "," << a.sizes.device_bytes << "," << a.sizes.storage_bytes << "," << (a.sizes.device_bytes_estimated ? 1 : 0) << "\n";
  os << "built_at_ns=" << a.stored_ns << "\n";
  os << "key_canonical_b64=" << b64encode(std::string(a.key.canonical_bytes().begin(), a.key.canonical_bytes().end())) << "\n";
  return os.str();
}
}  // namespace

Result<void> FilePersistenceStore::open(const std::string& root) {
  if (root.empty()) return Result<void>(ErrorCode::InvalidArgument, "empty persistence root");
  std::error_code ec;
  fs::create_directories(root, ec);
  if (ec) return Result<void>(ErrorCode::IoError, "cannot create root: " + ec.message());
  fs::path rootp(root);
  meta_dir_ = (rootp / "artifacts").string();
  blob_dir_ = (rootp / "artifacts").string();
  fs::create_directories(meta_dir_, ec);
  fs::create_directories(rootp / "tmp", ec);
  fs::path format = rootp / "KC-FORMAT";
  if (!fs::exists(format)) { if (!write_file(format, kFormatMagic)) return Result<void>(ErrorCode::IoError, "cannot write format marker"); }
  else { std::string cur = read_file(format, nullptr); while (!cur.empty() && (cur.back() == '\n' || cur.back() == '\r')) cur.pop_back(); if (cur != kFormatMagic) return Result<void>(ErrorCode::UnknownMetadataVersion, "unknown persistence format"); }
  root_ = rootp.string();
  return Result<void>();
}

Result<void> FilePersistenceStore::put(const StoredArtifact& artifact) {
  if (root_.empty()) return Result<void>(ErrorCode::InvalidArgument, "store not open");
  fs::path rootp(root_);
  std::string idh = artifact.id.str();
  fs::path meta = rootp / "artifacts" / (idh + ".meta");
  fs::path blob = rootp / "artifacts" / (idh + ".blob");
  fs::path tmpdir = rootp / "tmp";
  auto token = std::to_string(now_ns()) + "-" + std::to_string(std::rand());
  fs::path btmp = tmpdir / (idh + ".blob.tmp." + token);
  { std::ofstream f(btmp, std::ios::binary | std::ios::trunc); if (!f) return Result<void>(ErrorCode::IoError, "cannot open blob temp");
    f.write(reinterpret_cast<const char*>(artifact.bytes.data()), static_cast<std::streamsize>(artifact.bytes.size())); f.flush(); }
  std::error_code ec;
  fs::rename(btmp, blob, ec); if (ec) { fs::remove(blob, ec); fs::rename(btmp, blob, ec); }
  if (ec) return Result<void>(ErrorCode::IoError, "cannot install blob: " + ec.message());
  std::string meta_text = build_meta(artifact);
  if (meta_text.empty()) return Result<void>(ErrorCode::InvalidArtifact, "cannot serialize key");
  std::string meta_sha = sha256_hex_str(meta_text);
  std::string full_meta = meta_text + "meta_sha256=" + meta_sha + "\n";
  fs::path mtmp = tmpdir / (idh + ".meta.tmp." + token);
  if (!write_file(mtmp, full_meta)) return Result<void>(ErrorCode::IoError, "cannot write meta temp");
  fs::rename(mtmp, meta, ec); if (ec) { fs::remove(meta, ec); fs::rename(mtmp, meta, ec); }
  if (ec) return Result<void>(ErrorCode::IoError, "cannot install meta: " + ec.message());
  fs::remove(btmp, ec);
  return Result<void>();
}

namespace {
Result<StoredArtifact> parse_artifact_inner(const std::string& idh, const std::string& meta_text, const fs::path& blob) {
  std::istringstream in(meta_text);
  std::string line;
  if (!std::getline(in, line)) return Result<StoredArtifact>(ErrorCode::TruncatedData, "empty metadata");
  while (!line.empty() && line.back() == '\r') line.pop_back();
  if (line != kMetaMagic) return Result<StoredArtifact>(ErrorCode::UnknownMetadataVersion, "unknown metadata version: " + line);
  std::vector<std::string> lines;
  std::string stored_sha;
  std::string body = line + "\n";   // the magic line is part of the checksummed body
  while (std::getline(in, line)) {
    while (!line.empty() && line.back() == '\r') line.pop_back();
    if (field_ok(line, "meta_sha256")) { stored_sha = line.substr(std::string("meta_sha256=").size()); if (stored_sha.size() != 64) return Result<StoredArtifact>(ErrorCode::ChecksumMismatch, "malformed meta_sha256"); continue; }
    body += line; body += "\n"; lines.push_back(line);
  }
  std::string recomputed = sha256_hex_str(body);
  if (stored_sha.empty()) return Result<StoredArtifact>(ErrorCode::ChecksumMismatch, "missing meta_sha256");
  if (recomputed != stored_sha) return Result<StoredArtifact>(ErrorCode::ChecksumMismatch, "metadata checksum mismatch");
  StoredArtifact a; std::uint64_t len = 0;
  (void)idh;
  try {
  for (auto& l : lines) {
    if (field_ok(l, "id")) { auto pid = StrongId128::parse(l.substr(3)); if (!pid) return Result<StoredArtifact>(ErrorCode::MalformedFrame, "bad id"); a.id = ArtifactId(pid.value().hi(), pid.value().lo()); }
    else if (field_ok(l, "generation")) { a.generation.value = std::stoull(l.substr(11)); }
    else if (field_ok(l, "format")) { a.format = b64decode(l.substr(7)); }
    else if (field_ok(l, "namespace")) { a.namespace_ = b64decode(l.substr(10)); }
    else if (field_ok(l, "backend")) { a.provenance.producer = b64decode(l.substr(8)); }
    else if (field_ok(l, "operation")) { a.key.operation = b64decode(l.substr(10)); }
    else if (field_ok(l, "tenant")) { a.provenance.tenant = b64decode(l.substr(7)); }
    else if (field_ok(l, "artifact_sha256")) { a.sha256_hex = l.substr(16); }
    else if (field_ok(l, "sizes")) { std::istringstream s(l.substr(6)); std::string t; int idx = 0; while (std::getline(s, t, ',')) { std::uint64_t v = std::stoull(t); if (idx == 0) a.sizes.artifact_bytes = v; else if (idx == 1) a.sizes.host_bytes = v; else if (idx == 2) a.sizes.device_bytes = v; else if (idx == 3) a.sizes.storage_bytes = v; else if (idx == 4) a.sizes.device_bytes_estimated = (v != 0); ++idx; } }
    else if (field_ok(l, "built_at_ns")) { a.stored_ns = std::stoull(l.substr(12)); }
    else if (field_ok(l, "key_canonical_b64")) { std::string kb = b64decode(l.substr(18)); if (kb.empty()) return Result<StoredArtifact>(ErrorCode::MalformedFrame, "bad key canonical"); auto r = KernelCompatibilityKey::from_canonical(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(kb.data()), kb.size())); if (!r) return Result<StoredArtifact>(ErrorCode::MalformedFrame, "bad key canonical: " + r.error().message()); a.key = std::move(r.value()); }
  }
  } catch (...) { return Result<StoredArtifact>(ErrorCode::MalformedFrame, "malformed metadata field"); }
  std::string blob_str = read_file(blob, &len);
  if (blob_str.empty() && len == 0) return Result<StoredArtifact>(ErrorCode::TruncatedData, "blob missing or empty");
  if (len != a.sizes.artifact_bytes) return Result<StoredArtifact>(ErrorCode::TruncatedData, "blob length mismatch: expected " + std::to_string(a.sizes.artifact_bytes) + " got " + std::to_string(len));
  a.bytes.assign(blob_str.begin(), blob_str.end());
  std::string blob_sha = sha256_hex_bytes(a.bytes);
  if (a.sha256_hex && !a.sha256_hex->empty() && *a.sha256_hex != blob_sha) return Result<StoredArtifact>(ErrorCode::ChecksumMismatch, "artifact checksum mismatch");
  return a;
}
}  // namespace

Result<StoredArtifact> FilePersistenceStore::get(const ArtifactId& id) {
  if (root_.empty()) return Result<StoredArtifact>(ErrorCode::InvalidArgument, "store not open");
  fs::path rootp(root_); std::string idh = id.str();
  fs::path meta = rootp / "artifacts" / (idh + ".meta");
  fs::path blob = rootp / "artifacts" / (idh + ".blob");
  std::string meta_text = read_file(meta, nullptr);
  if (meta_text.empty()) return Result<StoredArtifact>(ErrorCode::NotFound, "meta missing");
  return parse_artifact_inner(idh, meta_text, blob);
}

Result<void> FilePersistenceStore::remove(const ArtifactId& id) {
  if (root_.empty()) return Result<void>(ErrorCode::InvalidArgument, "store not open");
  fs::path rootp(root_); std::string idh = id.str(); std::error_code ec;
  fs::remove(rootp / "artifacts" / (idh + ".meta"), ec);
  fs::remove(rootp / "artifacts" / (idh + ".blob"), ec);
  return Result<void>();
}

Result<std::vector<ArtifactId>> FilePersistenceStore::list() {
  std::vector<ArtifactId> out;
  if (root_.empty()) return out;
  fs::path dir(root_ + "/artifacts"); std::error_code ec;
  if (!fs::exists(dir, ec)) return out;
  for (auto& e : fs::directory_iterator(dir, ec)) {
    if (e.is_regular_file(ec) && e.path().extension() == ".meta") {
      std::string name = e.path().stem().string();
      auto r = StrongId128::parse(name); if (r) out.push_back(ArtifactId(r.value().hi(), r.value().lo()));
    }
  }
  return out;
}

Result<std::vector<StoredArtifact>> FilePersistenceStore::recover(std::vector<std::string>* rejected, std::vector<std::string>* orphans) {
  std::vector<StoredArtifact> valid;
  if (root_.empty()) return Result<std::vector<StoredArtifact>>(ErrorCode::InvalidArgument, "store not open");
  fs::path rootp(root_); std::error_code ec;
  fs::path tmpdir = rootp / "tmp";
  if (fs::exists(tmpdir, ec)) {
    for (auto& e : fs::directory_iterator(tmpdir, ec)) {
      if (e.is_regular_file(ec)) { std::string fname = e.path().filename().string(); if (orphans) orphans->push_back((tmpdir / fname).string()); fs::remove(e.path(), ec); }
    }
  }
  fs::path dir = rootp / "artifacts";
  if (!fs::exists(dir, ec)) return valid;
  for (auto& e : fs::directory_iterator(dir, ec)) {
    if (!(e.is_regular_file(ec) && e.path().extension() == ".meta")) continue;
    std::string idh = e.path().stem().string();
    fs::path blob = dir / (idh + ".blob");
    std::string meta_text = read_file(e.path(), nullptr);
    if (meta_text.empty()) { if (rejected) rejected->push_back("orphan-meta " + idh + " (empty)"); continue; }
    auto r = parse_artifact_inner(idh, meta_text, blob);
    if (!r) { if (rejected) rejected->push_back(idh + " :: " + r.error().message()); continue; }
    valid.push_back(std::move(r.value()));
  }
  return valid;
}

Result<void> FilePersistenceStore::close() { return Result<void>(); }

}  // namespace kernelcache