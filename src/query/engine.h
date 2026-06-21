#pragma once

#include "../storage/engine.h"
#include "../storage/record.h"

#include <cstddef>
#include <string>
#include <vector>

namespace silo::query {

struct SearchResult {
  std::string id;
  std::string sic_hex;
  float score;
};

class QueryEngine {
 public:
  explicit QueryEngine(storage::StorageEngine& engine) : engine_(engine) {}

  std::vector<SearchResult> search(const std::vector<float>& query, int top_k, bool use_cosine = true);

 private:
  storage::StorageEngine& engine_;
};

} // namespace silo::query
