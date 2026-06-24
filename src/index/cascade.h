#pragma once

#include <memory>
#include <string>
#include <vector>

namespace silo::index {

struct CascadeNode {
  int level = 0;
  int num_centroids = 0;
  std::vector<float> centroids;
  std::vector<CascadeNode*> children;
  std::vector<int> vector_ids;

  CascadeNode() = default;
  ~CascadeNode();

  CascadeNode(const CascadeNode&) = delete;
  CascadeNode& operator=(const CascadeNode&) = delete;
};

struct CascadeTree {
  std::unique_ptr<CascadeNode> root;
  int dimension = 0;
  int num_vectors = 0;
  std::vector<float> tree_mean;
};

struct CascadeResult {
  int index;
  float score;
};

class CascadeEngine {
 public:
  CascadeEngine() = default;
  ~CascadeEngine() = default;

  CascadeEngine(const CascadeEngine&) = delete;
  CascadeEngine& operator=(const CascadeEngine&) = delete;
  CascadeEngine(CascadeEngine&&) = default;
  CascadeEngine& operator=(CascadeEngine&&) = default;

  void build(const std::vector<std::vector<float>>& vectors, int dimension);

  std::vector<CascadeResult> search(const std::vector<float>& query,
                                    int top_k,
                                    int num_trees = 3,
                                    int beam_width = 3);

  bool is_built() const { return !trees_.empty(); }
  int num_trees() const { return trees_.size(); }
  int total_vectors() const { return num_vectors_; }

  const std::vector<float>& vectors_flat() const { return vectors_flat_; }
  int dimension() const { return dimension_; }

 private:
  std::vector<float> vectors_flat_;
  std::vector<CascadeTree> trees_;
  int dimension_ = 0;
  int num_vectors_ = 0;

  CascadeNode* build_node(const std::vector<int>& indices, int count, int level);
  void kmeans(const std::vector<int>& indices, int count, int k,
              std::vector<float>& centroids_out,
              std::vector<int>& assignments_out);
  void compute_mean(const std::vector<int>& indices, int count,
                    std::vector<float>& mean_out);
  void search_tree_greedy(const CascadeTree& tree, const float* query,
                          std::vector<CascadeResult>& results) const;
  void search_tree_multiprobe(const CascadeTree& tree, const float* query,
                              int beam_width,
                              std::vector<CascadeResult>& results) const;
};

} // namespace silo::index
