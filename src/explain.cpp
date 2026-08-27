// explain.cpp - structured Explain rendering.
#include "kernelcache/explain.hpp"
#include <sstream>

namespace kernelcache {

namespace {
std::string esc(const std::string& s) {
  std::string o; for (char c : s) {
    if (c == '\\') o += "\\\\"; else if (c == '"') o += "\\\""; else if (c == '\n') o += "\\n"; else o += c;
  }
  return o;
}
}  // namespace

std::string to_json(const Explain& ex) {
  std::ostringstream os;
  os << "{\"ok\":" << (ex.ok ? "true" : "false") << ",\"text\":\"" << esc(ex.text) << "\",\"nodes\":[";
  for (std::size_t i = 0; i < ex.nodes.size(); ++i) {
    if (i) os << ",";
    const auto& n = ex.nodes[i];
    os << "{\"topic\":\"" << esc(n.topic) << "\",\"summary\":\"" << esc(n.summary) << "\",\"details\":[";
    for (std::size_t j = 0; j < n.details.size(); ++j) { if (j) os << ","; os << "\"" << esc(n.details[j]) << "\""; }
    os << "]}";
  }
  os << "]}";
  return os.str();
}

}  // namespace kernelcache
