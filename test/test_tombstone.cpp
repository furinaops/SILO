#include "test_main.h"
#include "../src/storage/engine.h"
#include "../src/crypto/sic.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>

TEST_CASE("Tombstone marks record as deleted") {
  silo::storage::StorageEngine engine;
  std::string dir = "/tmp/silo_test_tombstone_" + std::to_string(std::time(nullptr));
  engine.create(dir);

  silo::storage::Record rec;
  rec.id = "del-me";
  rec.timestamp = 1;
  rec.vector = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
  rec.dimension = 8;
  auto sic = silo::crypto::SICUtils::generate(rec);
  rec.sic = sic;
  engine.store(rec, sic);

  auto before = engine.load(sic);
  if (!before) return false;

  engine.tombstone(sic);
  auto after = engine.load(sic);

  std::filesystem::remove_all(dir);
  return !after.has_value();
}

TEST_CASE("Tombstoned records excluded from load_all") {
  silo::storage::StorageEngine engine;
  std::string dir = "/tmp/silo_test_tombstone_all_" + std::to_string(std::time(nullptr));
  engine.create(dir);

  std::vector<silo::crypto::SIC> sics;
  for (int i = 0; i < 5; ++i) {
    silo::storage::Record rec;
    rec.id = "v" + std::to_string(i);
    rec.timestamp = i;
    rec.vector = {float(i), float(i+1), float(i+2), float(i+3),
                  float(i+4), float(i+5), float(i+6), float(i+7)};
    rec.dimension = 8;
    auto sic = silo::crypto::SICUtils::generate(rec);
    rec.sic = sic;
    engine.store(rec, sic);
    sics.push_back(sic);
  }

  engine.tombstone(sics[1]);
  engine.tombstone(sics[3]);

  auto all = engine.load_all();
  std::filesystem::remove_all(dir);
  return all.size() == 3;
}
