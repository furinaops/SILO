#pragma once

#include "../crypto/sic.h"
#include "page.h"
#include "wal.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace silo::storage {

struct PageSlot {
  uint32_t page_id;
  uint16_t slot_index;
};

struct StatusInfo {
  size_t live_count = 0;
  size_t tombstone_count = 0;
  size_t page_count = 0;
  size_t disk_usage = 0;
};

class StorageEngine {
 public:
  StorageEngine();
  ~StorageEngine();

  bool create(const std::string& db_dir);
  bool open(const std::string& db_dir);
  void close();

  PageSlot store(const Record& record, const crypto::SIC& sic);
  std::optional<Record> load(const crypto::SIC& sic);
  std::vector<Record> load_all();
  bool tombstone(const crypto::SIC& sic);
  size_t compact();

  StatusInfo status() const;
  std::string db_dir() const { return db_dir_; }

 private:
  Page* allocate_page();
  Page* get_page(uint32_t page_id);
  void build_cache();
  void sync_pages();

  std::string db_dir_;
  std::vector<Page*> pages_;
  std::unordered_map<crypto::SIC, PageSlot> sic_cache_;
  std::unordered_set<crypto::SIC> tombstone_cache_;
  WAL wal_;
};

} // namespace silo::storage
