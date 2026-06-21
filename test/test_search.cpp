#include "test_main.h"
#include "../src/query/engine.h"
#include "../src/query/simd.h"
#include "../src/storage/engine.h"
#include "../src/crypto/sic.h"

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>

TEST_CASE("Cosine similarity top result matches closest vector") {
  silo::storage::StorageEngine engine;
  std::string dir = "/tmp/silo_test_search_" + std::to_string(std::time(nullptr));
  engine.create(dir);

  // Insert a vector
  silo::storage::Record rec;
  rec.id = "target";
  rec.timestamp = 1;
  rec.vector = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  rec.dimension = 8;
  auto sic = silo::crypto::SICUtils::generate(rec);
  rec.sic = sic;
  engine.store(rec, sic);

  // Insert another
  silo::storage::Record rec2;
  rec2.id = "other";
  rec2.timestamp = 2;
  rec2.vector = {0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  rec2.dimension = 8;
  auto sic2 = silo::crypto::SICUtils::generate(rec2);
  rec2.sic = sic2;
  engine.store(rec2, sic2);

  silo::query::QueryEngine qe(engine);
  std::vector<float> query = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  auto results = qe.search(query, 5);

  std::filesystem::remove_all(dir);
  if (results.empty()) return false;
  return results[0].id == "target";
}

TEST_CASE("SIMD dot product matches scalar") {
  float a[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
  float b[8] = {8.0f, 7.0f, 6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f};

  float simd = silo::query::dot_product_simd(a, b, 8);
  float scalar = silo::query::dot_product_scalar(a, b, 8);

  return std::abs(simd - scalar) < 1e-5f;
}
