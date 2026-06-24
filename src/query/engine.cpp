#include "engine.h"
#include "simd.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <queue>
#include <vector>

#ifdef __AVX2__
#include <immintrin.h>
#endif

namespace silo::query {

namespace {

struct Precomputed {
  const float* data;
  float norm;
};

struct HeapEntry {
  float score;
  const storage::Record* record;

  bool operator<(const HeapEntry& other) const {
    return score > other.score;
  }
};

void prefetch_range(const void* addr, size_t len) {
#ifdef __AVX2__
  const char* p = static_cast<const char*>(addr);
  const char* end = p + len;
  for (; p < end; p += 64) {
    _mm_prefetch(p, _MM_HINT_T0);
  }
#endif
}

} // namespace

std::vector<SearchResult> QueryEngine::search(const std::vector<float>& query, int top_k, bool use_cosine) {
  auto records = engine_.load_all();

  if (records.empty()) return {};

  std::priority_queue<HeapEntry> heap;

  float query_norm = 0.0f;
  if (use_cosine) {
    for (size_t i = 0; i < query.size(); ++i) {
      query_norm += query[i] * query[i];
    }
    query_norm = std::sqrt(query_norm);
    if (query_norm < 1e-10f) query_norm = 1.0f;
  }

#ifdef __AVX2__
  size_t dim = query.size();
  const float* qdata = query.data();

  for (auto& rec : records) {
    if (rec.vector.size() != dim) continue;

    prefetch_range(rec.vector.data(), dim * sizeof(float));

    float score;
    if (use_cosine) {
      float dot = dot_product_simd(qdata, rec.vector.data(), dim);
      float rec_norm = 0.0f;
      for (size_t i = 0; i < dim; ++i) {
        rec_norm += rec.vector[i] * rec.vector[i];
      }
      rec_norm = std::sqrt(rec_norm);
      if (rec_norm < 1e-10f) rec_norm = 1.0f;
      score = dot / (query_norm * rec_norm);
    } else {
      score = -euclidean_distance(qdata, rec.vector.data(), dim);
    }

    if (static_cast<int>(heap.size()) < top_k) {
      heap.push({score, &rec});
    } else if (score > heap.top().score) {
      heap.pop();
      heap.push({score, &rec});
    }
  }
#else
  size_t dim = query.size();
  const float* qdata = query.data();

  std::vector<Precomputed> precomputed;
  precomputed.reserve(records.size());

  for (auto& rec : records) {
    if (rec.vector.size() != dim) continue;
    float norm = 0.0f;
    if (use_cosine) {
      for (size_t i = 0; i < dim; ++i) {
        norm += rec.vector[i] * rec.vector[i];
      }
      norm = std::sqrt(norm);
      if (norm < 1e-10f) norm = 1.0f;
    }
    precomputed.push_back({rec.vector.data(), norm});
  }

  for (size_t i = 0; i < precomputed.size(); ++i) {
    float score;
    if (use_cosine) {
      float dot = dot_product_scalar(qdata, precomputed[i].data, dim);
      score = dot / (query_norm * precomputed[i].norm);
    } else {
      score = -euclidean_distance(qdata, precomputed[i].data, dim);
    }

    if (static_cast<int>(heap.size()) < top_k) {
      heap.push({score, &records[i]});
    } else if (score > heap.top().score) {
      heap.pop();
      heap.push({score, &records[i]});
    }
  }
#endif

  std::vector<SearchResult> results(heap.size());
  for (int i = static_cast<int>(heap.size()) - 1; i >= 0; --i) {
    results[i] = {heap.top().record->id,
                  crypto::SICUtils::to_string(heap.top().record->sic),
                  heap.top().score};
    heap.pop();
  }
  return results;
}

// ---------------------------------------------------------------------------
// Cascade index build
// ---------------------------------------------------------------------------

void QueryEngine::build_cascade() {
  cascade_records_ = engine_.load_all();
  if (cascade_records_.empty()) return;

  std::vector<std::vector<float>> vectors;
  vectors.reserve(cascade_records_.size());
  for (auto& rec : cascade_records_) {
    vectors.push_back(rec.vector);
  }

  int dim = static_cast<int>(cascade_records_[0].dimension);
  cascade_.build(vectors, dim);
}

// ---------------------------------------------------------------------------
// Cascade search
// ---------------------------------------------------------------------------

std::vector<SearchResult> QueryEngine::search_cascade(
    const std::vector<float>& query, int top_k, int num_trees, int beam_width) {
  if (!cascade_.is_built()) return {};

  auto cascade_results = cascade_.search(query, top_k, num_trees, beam_width);
  std::vector<SearchResult> results;
  results.reserve(cascade_results.size());

  for (auto& cr : cascade_results) {
    auto& rec = cascade_records_[cr.index];
    results.push_back({rec.id,
                       crypto::SICUtils::to_string(rec.sic),
                       cr.score});
  }
  return results;
}

} // namespace silo::query
