// explain.hpp - structured explain output.
#pragma once

#include <string>
#include <vector>

namespace kernelcache {

struct ExplainNode {
  std::string topic;     // "compatibility", "residency", ...
  std::string summary;
  std::vector<std::string> details;
};

struct Explain {
  std::string text;                 // human-readable
  std::string json;                 // JSON structure
  std::vector<ExplainNode> nodes;
  bool ok = true;
};

// Render an Explain as a compact JSON document.
std::string to_json(const Explain& ex);

}  // namespace kernelcache