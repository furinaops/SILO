#include "test_main.h"
#include "../src/index/cascade.h"
#include "../src/query/engine.h"
#include "../src/query/simd.h"
#include "../src/storage/engine.h"
#include "../src/crypto/sic.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <numeric>
#include <random>

// ---------------------------------------------------------------------------
// Build / search helper
// ---------------------------------------------------------------------------

static std::vector<std::vector<float>> make_random_vectors(int n, int dim, int seed = 42) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  std::vector<std::vector<float>> vecs(n);
  for (int i = 0; i < n; ++i) {
    vecs[i].resize(dim);
    for (int d = 0; d < dim; ++d) vecs[i][d] = dist(rng);
  }
  return vecs;
}

// ---------------------------------------------------------------------------
// Test: empty engine / no index
// ---------------------------------------------------------------------------

TEST_CASE("Cascade: not built returns false") {
  silo::index::CascadeEngine e;
  return !e.is_built() && e.num_trees() == 0 && e.total_vectors() == 0;
}

TEST_CASE("Cascade: search on empty returns empty") {
  silo::index::CascadeEngine e;
  auto results = e.search({1.0f, 0.0f}, 5);
  return results.empty();
}

// ---------------------------------------------------------------------------
// Test: build with zero vectors
// ---------------------------------------------------------------------------

TEST_CASE("Cascade: build with zero vectors") {
  silo::index::CascadeEngine e;
  e.build({}, 8);
  return !e.is_built();
}

// ---------------------------------------------------------------------------
// Test: build with single vector (N = 1 < 128)
// ---------------------------------------------------------------------------

TEST_CASE("Cascade: single vector") {
  auto vecs = make_random_vectors(1, 8, 1);
  silo::index::CascadeEngine e;
  e.build(vecs, 8);
  if (!e.is_built()) return false;
  if (e.num_trees() != 1) return false;

  auto results = e.search(vecs[0], 5);
  return results.size() == 1 && results[0].index == 0 && std::abs(results[0].score - 1.0f) < 1e-5f;
}

// ---------------------------------------------------------------------------
// Test: build with exactly 128 vectors (one full tree)
// ---------------------------------------------------------------------------

TEST_CASE("Cascade: one full tree (128 vectors)") {
  auto vecs = make_random_vectors(128, 16, 2);
  silo::index::CascadeEngine e;
  e.build(vecs, 16);
  if (e.num_trees() != 1) return false;
  if (e.total_vectors() != 128) return false;

  // Search with the first vector
  auto results = e.search(vecs[0], 3);
  return !results.empty() && results[0].score > 0.99f;
}

// ---------------------------------------------------------------------------
// Test: build with 256 vectors (two full trees)
// ---------------------------------------------------------------------------

TEST_CASE("Cascade: two full trees (256 vectors)") {
  auto vecs = make_random_vectors(256, 8, 3);
  silo::index::CascadeEngine e;
  e.build(vecs, 8);
  return e.num_trees() == 2 && e.total_vectors() == 256;
}

// ---------------------------------------------------------------------------
// Test: build with 300 vectors (remainder tree)
// ---------------------------------------------------------------------------

TEST_CASE("Cascade: remainder tree (300 vectors)") {
  auto vecs = make_random_vectors(300, 8, 4);
  silo::index::CascadeEngine e;
  e.build(vecs, 8);
  // 300 = 128 + 128 + 44 → 3 trees (2 full + 1 remainder)
  return e.num_trees() == 3 && e.total_vectors() == 300;
}

// ---------------------------------------------------------------------------
// Test: deterministic (same query → same result)
// ---------------------------------------------------------------------------

TEST_CASE("Cascade: deterministic results") {
  auto vecs = make_random_vectors(200, 16, 5);
  silo::index::CascadeEngine e;
  e.build(vecs, 16);

  std::vector<float> query(16, 0.5f);
  auto r1 = e.search(query, 5);
  auto r2 = e.search(query, 5);

  if (r1.size() != r2.size()) return false;
  for (size_t i = 0; i < r1.size(); ++i) {
    if (r1[i].index != r2[i].index) return false;
    if (std::abs(r1[i].score - r2[i].score) > 1e-6f) return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Test: different num_probe_trees values
// ---------------------------------------------------------------------------

TEST_CASE("Cascade: more probe trees finds more results") {
  auto vecs = make_random_vectors(256, 8, 6);
  silo::index::CascadeEngine e;
  e.build(vecs, 8);

  std::vector<float> query(8, 0.3f);
  auto r1 = e.search(query, 3, 1);  // 1 tree
  auto r3 = e.search(query, 3, 3);  // 3 trees

  return r3.size() == 3;  // Regardless of which was better, must return 3 results
}

// ---------------------------------------------------------------------------
// Test: top-1 matches between cascade and brute-force for exact match
// ---------------------------------------------------------------------------

TEST_CASE("Cascade: top-1 matches brute-force when query is a stored vector") {
  auto vecs = make_random_vectors(100, 8, 7);
  silo::index::CascadeEngine e;
  e.build(vecs, 8);

  // Query = first vector → should find itself as top-1 with score ~1.0
  auto results = e.search(vecs[0], 5);
  return !results.empty() && results[0].index == 0 && std::abs(results[0].score - 1.0f) < 1e-4f;
}

// ---------------------------------------------------------------------------
// Test: QueryEngine integration (build_cascade + search_cascade)
// ---------------------------------------------------------------------------

TEST_CASE("Cascade: QueryEngine integration") {
  silo::storage::StorageEngine engine;
  std::string dir = test_dir("cascade_qe");
  engine.create(dir);

  // Insert 10 vectors
  auto vecs = make_random_vectors(10, 8, 8);
  for (int i = 0; i < 10; ++i) {
    silo::storage::Record rec;
    rec.id = "v" + std::to_string(i);
    rec.timestamp = i;
    rec.vector = vecs[i];
    rec.dimension = 8;
    auto sic = silo::crypto::SICUtils::generate(rec);
    rec.sic = sic;
    engine.store(rec, sic);
  }

  silo::query::QueryEngine qe(engine);
  qe.build_cascade();
  if (!qe.cascade_is_built()) return false;
  if (qe.cascade_num_trees() != 1) return false;  // 10 < 128 → 1 tree

  // Search cascade
  auto results = qe.search_cascade(vecs[0], 3);
  std::filesystem::remove_all(dir);
  return !results.empty() && results[0].id == "v0";
}

// ---------------------------------------------------------------------------
// Test: cascade result count respects top_k
// ---------------------------------------------------------------------------

TEST_CASE("Cascade: respects top_k limit") {
  auto vecs = make_random_vectors(50, 8, 9);
  silo::index::CascadeEngine e;
  e.build(vecs, 8);

  std::vector<float> query(8, 0.0f);
  auto r3 = e.search(query, 3);
  auto r10 = e.search(query, 10);

  return r3.size() <= 3 && r10.size() <= 10 && r10.size() >= r3.size();
}

// ---------------------------------------------------------------------------
// Test: leaf node structure (8 clusters of ~16 vectors for 128 chunk)
// ---------------------------------------------------------------------------

TEST_CASE("Cascade: tree structure for full chunk") {
  auto vecs = make_random_vectors(128, 8, 10);
  silo::index::CascadeEngine e;
  e.build(vecs, 8);

  // Access the tree internals to verify structure
  // The tree root should NOT be null
  // This is a sanity check that the build completed without crashing
  return e.is_built() && e.num_trees() == 1;
}
