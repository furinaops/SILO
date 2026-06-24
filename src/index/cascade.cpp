#include "cascade.h"

#include "../query/simd.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>

namespace silo::index {

// ---------------------------------------------------------------------------
// CascadeNode destructor
// ---------------------------------------------------------------------------

CascadeNode::~CascadeNode() {
  for (auto* child : children) delete child;
}

// ---------------------------------------------------------------------------
// K-means (deterministic, fixed 20 iterations)
// ---------------------------------------------------------------------------

void CascadeEngine::kmeans(const std::vector<int>& indices, int count, int k,
                           std::vector<float>& centroids_out,
                           std::vector<int>& assignments_out) {
  centroids_out.resize(k * dimension_);
  assignments_out.resize(count);

  if (count == 0) return;

  // Initialise: first k vectors (deterministic)
  for (int i = 0; i < k; ++i) {
    int src = i % count;
    std::memcpy(&centroids_out[i * dimension_],
                &vectors_flat_[indices[src] * dimension_],
                dimension_ * sizeof(float));
  }

  std::vector<float> new_centroids(k * dimension_, 0.0f);
  std::vector<int> counts(k, 0);

  for (int iter = 0; iter < 20; ++iter) {
    // ----- assignment step -----
    for (int i = 0; i < count; ++i) {
      const float* vec = &vectors_flat_[indices[i] * dimension_];
      float best_dot = -std::numeric_limits<float>::max();
      int best_idx = 0;
      for (int j = 0; j < k; ++j) {
        float dot = query::dot_product_simd(vec, &centroids_out[j * dimension_], dimension_);
        if (dot > best_dot) {
          best_dot = dot;
          best_idx = j;
        }
      }
      assignments_out[i] = best_idx;
    }

    // ----- update step -----
    std::fill(new_centroids.begin(), new_centroids.end(), 0.0f);
    std::fill(counts.begin(), counts.end(), 0);

    for (int i = 0; i < count; ++i) {
      int c = assignments_out[i];
      counts[c]++;
      const float* vec = &vectors_flat_[indices[i] * dimension_];
      float* dst = &new_centroids[c * dimension_];
      for (int d = 0; d < dimension_; ++d) {
        dst[d] += vec[d];
      }
    }

    bool changed = false;
    for (int j = 0; j < k; ++j) {
      if (counts[j] > 0) {
        float inv = 1.0f / counts[j];
        float* dst = &centroids_out[j * dimension_];
        const float* src = &new_centroids[j * dimension_];
        for (int d = 0; d < dimension_; ++d) {
          float old = dst[d];
          dst[d] = src[d] * inv;
          if (std::abs(dst[d] - old) > 1e-6f) changed = true;
        }
      }
    }

    if (!changed) break;
  }
}

// ---------------------------------------------------------------------------
// Compute mean vector over a set of indices
// ---------------------------------------------------------------------------

void CascadeEngine::compute_mean(const std::vector<int>& indices, int count,
                                 std::vector<float>& mean_out) {
  mean_out.assign(dimension_, 0.0f);
  if (count == 0) return;
  for (int i = 0; i < count; ++i) {
    const float* vec = &vectors_flat_[indices[i] * dimension_];
    for (int d = 0; d < dimension_; ++d) mean_out[d] += vec[d];
  }
  float inv = 1.0f / count;
  for (int d = 0; d < dimension_; ++d) mean_out[d] *= inv;
}

// ---------------------------------------------------------------------------
// Recursive tree builder
// ---------------------------------------------------------------------------

CascadeNode* CascadeEngine::build_node(const std::vector<int>& indices,
                                       int count, int level) {
  auto* node = new CascadeNode();
  node->level = level;

  if (count <= 16 || level == 3) {
    // Leaf node: store actual vector indices
    node->vector_ids.assign(indices.begin(), indices.begin() + count);
    return node;
  }

  // Number of centroids at this level: 128, 64, or 32
  int nc = 128 >> level;

  // Run k-means
  std::vector<float> centroids;
  std::vector<int> assignments;
  kmeans(indices, count, nc, centroids, assignments);

  node->centroids = std::move(centroids);
  node->num_centroids = nc;

  // Split vectors into two groups by centroid index range
  std::vector<int> left_ids, right_ids;
  left_ids.reserve(count);
  right_ids.reserve(count);

  int half = nc / 2;
  for (int i = 0; i < count; ++i) {
    if (assignments[i] < half) {
      left_ids.push_back(indices[i]);
    } else {
      right_ids.push_back(indices[i]);
    }
  }

  node->children.push_back(build_node(left_ids, left_ids.size(), level + 1));
  node->children.push_back(build_node(right_ids, right_ids.size(), level + 1));

  return node;
}

// ---------------------------------------------------------------------------
// Public build
// ---------------------------------------------------------------------------

void CascadeEngine::build(const std::vector<std::vector<float>>& vectors,
                          int dimension) {
  trees_.clear();
  vectors_flat_.clear();
  dimension_ = dimension;
  num_vectors_ = vectors.size();

  if (num_vectors_ == 0) return;

  // Flatten vectors
  vectors_flat_.resize(num_vectors_ * dimension);
  for (int i = 0; i < num_vectors_; ++i) {
    std::memcpy(&vectors_flat_[i * dimension], vectors[i].data(),
                dimension * sizeof(float));
  }

  // Chunk by 128 and build one tree per chunk
  std::vector<int> all_indices(num_vectors_);
  std::iota(all_indices.begin(), all_indices.end(), 0);

  for (int start = 0; start < num_vectors_; start += 128) {
    int count = std::min(128, num_vectors_ - start);

    CascadeTree tree;
    tree.dimension = dimension;
    tree.num_vectors = count;

    std::vector<int> chunk(all_indices.begin() + start,
                           all_indices.begin() + start + count);
    tree.root.reset(build_node(chunk, count, 0));

    compute_mean(chunk, count, tree.tree_mean);
    trees_.push_back(std::move(tree));
  }
}

// ---------------------------------------------------------------------------
// Greedy descent on a single tree
// ---------------------------------------------------------------------------

void CascadeEngine::search_tree_greedy(const CascadeTree& tree, const float* query,
                                std::vector<CascadeResult>& results) const {
  const CascadeNode* node = tree.root.get();

  // Descend through levels 0, 1, 2
  while (node && node->level < 3 && node->num_centroids > 0) {
    int nc = node->num_centroids;
    const float* centroids = node->centroids.data();

    float best_sim = -std::numeric_limits<float>::max();
    int best_idx = 0;

    for (int i = 0; i < nc; ++i) {
      float sim = query::cosine_similarity(query, &centroids[i * dimension_], dimension_);
      if (sim > best_sim) {
        best_sim = sim;
        best_idx = i;
      }
    }

    // Descend: first half of centroids → child 0, second half → child 1
    int child_idx = (best_idx < nc / 2) ? 0 : 1;
    node = (child_idx < static_cast<int>(node->children.size()))
               ? node->children[child_idx]
               : nullptr;
  }

  // Leaf: compute exact distances
  if (!node) return;
  for (int vid : node->vector_ids) {
    float sim = query::cosine_similarity(query, &vectors_flat_[vid * dimension_], dimension_);
    results.push_back({vid, sim});
  }
}

// ---------------------------------------------------------------------------
// Multi-probe descent (beam search) on a single tree
// ---------------------------------------------------------------------------

void CascadeEngine::search_tree_multiprobe(const CascadeTree& tree,
                                           const float* query,
                                           int beam_width,
                                           std::vector<CascadeResult>& results) const {
  // beam: (node, placeholder_score)
  using BeamEntry = std::pair<const CascadeNode*, float>;
  std::vector<BeamEntry> beam;
  beam.emplace_back(tree.root.get(), 0.0f);

  for (int level = 0; level < 3; ++level) {
    std::vector<BeamEntry> candidates;

    for (auto& entry : beam) {
      const CascadeNode* node = entry.first;
      if (!node || node->num_centroids == 0) continue;
      int nc = node->num_centroids;
      const float* cents = node->centroids.data();

      for (int i = 0; i < nc; ++i) {
        float sim = query::cosine_similarity(query, &cents[i * dimension_], dimension_);
        int child_idx = (i < nc / 2) ? 0 : 1;
        if (child_idx < static_cast<int>(node->children.size())) {
          candidates.emplace_back(node->children[child_idx], sim);
        }
      }
    }

    if (candidates.empty()) break;

    std::sort(candidates.begin(), candidates.end(),
              [](const BeamEntry& a, const BeamEntry& b) {
                return a.second > b.second;
              });

    beam.clear();
    for (auto& c : candidates) {
      bool dup = false;
      for (auto& b : beam) {
        if (b.first == c.first) { dup = true; break; }
      }
      if (!dup) {
        beam.push_back(c);
        if (static_cast<int>(beam.size()) >= beam_width) break;
      }
    }
  }

  // Collect vectors from all leaf nodes in the beam
  for (auto& entry : beam) {
    const CascadeNode* leaf = entry.first;
    if (!leaf) continue;
    for (int vid : leaf->vector_ids) {
      float sim = query::cosine_similarity(query, &vectors_flat_[vid * dimension_], dimension_);
      results.push_back({vid, sim});
    }
  }
}

// ---------------------------------------------------------------------------
// Resolve --trees special values: auto (-1) → sqrt heuristic, all (0) → total
// ---------------------------------------------------------------------------

static int resolve_tree_count(int requested, int total) {
  if (requested == -1) {  // auto
    int heuristic = static_cast<int>(std::sqrt(static_cast<double>(total)));
    return std::max(3, std::min(heuristic, total));
  }
  if (requested == 0) return total;  // all
  if (requested > total) {
    std::cerr << "[WARN] Requested " << requested << " trees, only "
              << total << " exist. Using all.\n";
    return total;
  }
  return std::max(1, requested);
}

// ---------------------------------------------------------------------------
// Public search with tree pre-filter
// ---------------------------------------------------------------------------

std::vector<CascadeResult> CascadeEngine::search(const std::vector<float>& query,
                                                  int top_k,
                                                  int num_trees,
                                                  int beam_width) {
  std::vector<CascadeResult> all_results;
  if (trees_.empty() || num_vectors_ == 0) return all_results;

  const float* qdata = query.data();

  // Resolve special values and clamp
  int total = static_cast<int>(trees_.size());
  int probe = resolve_tree_count(num_trees, total);
  if (probe <= 0) probe = 1;

  // Pre-filter trees by comparing to tree mean vectors
  struct Candidate {
    int index;
    float score;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(total);

  for (int t = 0; t < total; ++t) {
    float sim = query::cosine_similarity(qdata, trees_[t].tree_mean.data(), dimension_);
    candidates.push_back({t, sim});
  }

  // Pick top-k trees
  std::partial_sort(candidates.begin(), candidates.begin() + std::min(probe, total),
                    candidates.end(),
                    [](const Candidate& a, const Candidate& b) {
                      return a.score > b.score;
                    });
  std::partial_sort(candidates.begin(), candidates.begin() + probe,
                    candidates.end(),
                    [](const Candidate& a, const Candidate& b) {
                      return a.score > b.score;
                    });

  // Descent in selected trees (greedy or multi-probe)
  all_results.reserve(probe * (beam_width > 1 ? 48 : 16));
  for (int i = 0; i < probe; ++i) {
    if (beam_width > 1) {
      search_tree_multiprobe(trees_[candidates[i].index], qdata, beam_width, all_results);
    } else {
      search_tree_greedy(trees_[candidates[i].index], qdata, all_results);
    }
  }

  // Global top-k via partial sort
  if (top_k >= static_cast<int>(all_results.size())) {
    std::sort(all_results.begin(), all_results.end(),
              [](const CascadeResult& a, const CascadeResult& b) {
                return a.score > b.score;
              });
    return all_results;
  }

  std::partial_sort(all_results.begin(), all_results.begin() + top_k,
                    all_results.end(),
                    [](const CascadeResult& a, const CascadeResult& b) {
                      return a.score > b.score;
                    });
  all_results.resize(top_k);
  return all_results;
}

} // namespace silo::index
