#include "test_main.h"
#include "../src/storage/wal.h"
#include "../src/storage/engine.h"
#include "../src/crypto/sic.h"

#include <cstdlib>
#include <filesystem>

TEST_CASE("WAL append and read back") {
  std::string dir = test_dir("wal_test");
  std::filesystem::create_directories(dir);

  silo::storage::WAL wal;
  if (!wal.open(dir)) return false;

  silo::storage::WalEntry e1;
  e1.type = silo::storage::WalEntryType::INSERT;
  e1.timestamp = 123;
  e1.id = "test-wal";
  e1.vector = {1.0f, 2.0f, 3.0f};
  wal.append(e1);

  silo::storage::WalEntry e2;
  e2.type = silo::storage::WalEntryType::DELETE;
  e2.timestamp = 456;
  e2.sic = silo::crypto::SIC{};
  e2.id = "del-sic";
  wal.append(e2);

  auto entries = wal.read_all();
  std::filesystem::remove_all(dir);
  return entries.size() == 2;
}

TEST_CASE("WAL crash recovery replay") {
  std::string dir = test_dir("wal_replay");

  {
    silo::storage::StorageEngine engine;
    engine.create(dir);

    for (int i = 0; i < 50; ++i) {
      silo::storage::Record rec;
      rec.id = "crash-" + std::to_string(i);
      rec.timestamp = i;
      rec.vector = {float(i), float(i+1), float(i+2), float(i+3),
                    float(i+4), float(i+5), float(i+6), float(i+7)};
      rec.dimension = 8;
      auto sic = silo::crypto::SICUtils::generate(rec);
      rec.sic = sic;
      engine.store(rec, sic);
    }
  }

  {
    silo::storage::StorageEngine engine;
    engine.open(dir);

    auto all = engine.load_all();
    std::filesystem::remove_all(dir);
    return all.size() >= 50;
  }
}
