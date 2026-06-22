#pragma once

#include "../index/cascade.h"
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

  void build_cascade();
  std::vector<SearchResult> search_cascade(const std::vector<float>& query, int top_k);
  bool cascade_is_built() const { return cascade_.is_built(); }
  int cascade_num_trees() const { return cascade_.num_trees(); }
  int cascade_total_vectors() const { return cascade_.total_vectors(); }

 private:
  storage::StorageEngine& engine_;
  index::CascadeEngine cascade_;
  std::vector<storage::Record> cascade_records_;
};

} // namespace silo::query
