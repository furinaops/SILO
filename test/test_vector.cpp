#include "test_main.h"
#include "../src/storage/engine.h"
#include "../src/storage/record.h"
#include "../src/crypto/sic.h"

#include <cstdlib>
#include <filesystem>

TEST_CASE("Store and load vector") {
  silo::storage::StorageEngine engine;
  std::string dir = test_dir("vector");
  engine.create(dir);

  silo::storage::Record rec;
  rec.id = "test-store-load";
  rec.timestamp = 42;
  rec.vector = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
  rec.dimension = 8;

  auto sic = silo::crypto::SICUtils::generate(rec);
  rec.sic = sic;
  engine.store(rec, sic);

  auto loaded = engine.load(sic);
  if (!loaded) return false;
  if (loaded->id != "test-store-load") return false;
  if (loaded->vector.size() != 8) return false;
  if (loaded->vector[0] != 1.0f) return false;

  std::filesystem::remove_all(dir);
  return true;
}

TEST_CASE("Store multiple vectors and load_all") {
  silo::storage::StorageEngine engine;
  std::string dir = test_dir("store_all");
  engine.create(dir);

  for (int i = 0; i < 10; ++i) {
    silo::storage::Record rec;
    rec.id = "vec-" + std::to_string(i);
    rec.timestamp = i;
    rec.vector = {float(i), float(i+1), float(i+2), float(i+3),
                  float(i+4), float(i+5), float(i+6), float(i+7)};
    rec.dimension = 8;
    auto sic = silo::crypto::SICUtils::generate(rec);
    rec.sic = sic;
    engine.store(rec, sic);
  }

  auto all = engine.load_all();
  std::filesystem::remove_all(dir);
  return all.size() == 10;
}
