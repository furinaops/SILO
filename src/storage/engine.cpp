#include "engine.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sys/mman.h>
#include <unistd.h>

namespace silo::storage {

namespace {

bool starts_with(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

StorageEngine::StorageEngine() {}

StorageEngine::~StorageEngine() { close(); }

bool StorageEngine::create(const std::string& db_dir) {
  close();
  db_dir_ = db_dir;
  std::filesystem::create_directories(db_dir_);

  if (!wal_.open(db_dir_)) return false;

  allocate_page();
  return true;
}

bool StorageEngine::open(const std::string& db_dir) {
  close();
  db_dir_ = db_dir;
  if (!std::filesystem::exists(db_dir_)) return false;

  if (!wal_.open(db_dir_)) return false;

  std::vector<std::string> page_files;
  for (const auto& entry : std::filesystem::directory_iterator(db_dir_)) {
    auto name = entry.path().filename().string();
    if (starts_with(name, "pages-") && ends_with(name, ".bin")) {
      page_files.push_back(entry.path().string());
    }
  }
  std::sort(page_files.begin(), page_files.end());

  for (const auto& path : page_files) {
    auto* page = new Page();
    std::ifstream file(path, std::ios::binary);
    if (file.is_open()) {
      file.read(reinterpret_cast<char*>(page->data_), kPageSize);
      file.close();
    }
    pages_.push_back(page);
  }

  if (pages_.empty()) {
    allocate_page();
  }

  auto wal_entries = wal_.read_all();
  for (const auto& entry : wal_entries) {
    if (entry.type == WalEntryType::INSERT) {
      Record rec;
      rec.id = entry.id;
      rec.timestamp = entry.timestamp;
      rec.vector = entry.vector;
      rec.dimension = entry.vector.size();
      store(rec, entry.sic);
    } else if (entry.type == WalEntryType::DELETE) {
      tombstone(entry.sic);
    }
  }

  build_cache();
  return true;
}

void StorageEngine::close() {
  if (!pages_.empty()) sync_pages();
  for (auto* page : pages_) {
    delete page;
  }
  pages_.clear();
  sic_cache_.clear();
  tombstone_cache_.clear();
  wal_.close();
}

Page* StorageEngine::allocate_page() {
  auto* page = new Page();
  page->clear();
  uint32_t page_id = pages_.size();
  page->header().page_id = page_id;
  pages_.push_back(page);

  std::string path = db_dir_ + "/pages-" + std::to_string(page_id) + ".bin";
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(page->data_), kPageSize);
  file.close();

  return page;
}

Page* StorageEngine::get_page(uint32_t page_id) {
  if (page_id >= pages_.size()) return nullptr;
  return pages_[page_id];
}

void StorageEngine::sync_pages() {
  for (size_t i = 0; i < pages_.size(); ++i) {
    std::string path = db_dir_ + "/pages-" + std::to_string(i) + ".bin";
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(pages_[i]->data_), kPageSize);
    file.close();
  }
}

void StorageEngine::build_cache() {
  sic_cache_.clear();
  tombstone_cache_.clear();
  for (uint32_t pid = 0; pid < pages_.size(); ++pid) {
    auto* page = pages_[pid];
    for (uint16_t i = 0; i < page->record_count(); ++i) {
      auto rec = page->read_record(i);
      if (rec) {
        if (rec->tombstone) {
          tombstone_cache_.insert(rec->sic);
        } else {
          sic_cache_[rec->sic] = {pid, i};
        }
      }
    }
  }
}

PageSlot StorageEngine::store(const Record& record, const crypto::SIC& sic) {
  size_t id_bytes = record.id.size();
  size_t vec_bytes = record.vector.size() * sizeof(float);
  size_t rec_size = sizeof(RecordOnDisk) + id_bytes + vec_bytes;

  for (auto* page : pages_) {
    if (page->can_fit(rec_size)) {
      page->write_record(record, sic);

      WalEntry we;
      we.type = WalEntryType::INSERT;
      we.timestamp = record.timestamp;
      we.sic = sic;
      we.id = record.id;
      we.vector = record.vector;
      wal_.append(we);

      PageSlot slot;
      slot.page_id = page->header().page_id;
      slot.slot_index = page->record_count() - 1;
      sic_cache_[sic] = slot;
      return slot;
    }
  }

  auto* page = allocate_page();
  page->write_record(record, sic);

  WalEntry we;
  we.type = WalEntryType::INSERT;
  we.timestamp = record.timestamp;
  we.sic = sic;
  we.id = record.id;
  we.vector = record.vector;
  wal_.append(we);

  PageSlot slot;
  slot.page_id = page->header().page_id;
  slot.slot_index = 0;
  sic_cache_[sic] = slot;
  return slot;
}

std::optional<Record> StorageEngine::load(const crypto::SIC& sic) {
  if (tombstone_cache_.count(sic)) return std::nullopt;

  auto it = sic_cache_.find(sic);
  if (it == sic_cache_.end()) return std::nullopt;

  auto* page = get_page(it->second.page_id);
  if (!page) return std::nullopt;

  return page->read_record(it->second.slot_index);
}

std::vector<Record> StorageEngine::load_all() {
  std::vector<Record> result;
  for (uint32_t pid = 0; pid < pages_.size(); ++pid) {
    auto* page = pages_[pid];
    for (uint16_t i = 0; i < page->record_count(); ++i) {
      auto rec = page->read_record(i);
      if (rec && !rec->tombstone) {
        result.push_back(std::move(*rec));
      }
    }
  }
  return result;
}

bool StorageEngine::tombstone(const crypto::SIC& sic) {
  auto it = sic_cache_.find(sic);
  if (it == sic_cache_.end()) return false;

  auto* page = get_page(it->second.page_id);
  if (!page) return false;

  if (!page->mark_tombstone(it->second.slot_index)) return false;

  sic_cache_.erase(it);
  tombstone_cache_.insert(sic);

  WalEntry we;
  we.type = WalEntryType::DELETE;
  we.timestamp = static_cast<uint64_t>(std::time(nullptr));
  we.sic = sic;
  wal_.append(we);

  return true;
}

size_t StorageEngine::compact() {
  auto live = load_all();
  size_t tombstone_count = 0;
  for (auto* page : pages_) {
    for (uint16_t i = 0; i < page->record_count(); ++i) {
      auto rec = page->read_record(i);
      if (rec && rec->tombstone) tombstone_count++;
    }
  }

  for (auto* page : pages_) delete page;
  pages_.clear();
  sic_cache_.clear();
  tombstone_cache_.clear();

  for (auto& rec : live) {
    rec.tombstone = false;
    auto sic = crypto::SICUtils::generate(rec);
    store(rec, sic);
  }

  WalEntry we;
  we.type = WalEntryType::COMPACT;
  we.timestamp = static_cast<uint64_t>(std::time(nullptr));
  wal_.append(we);

  sync_pages();
  wal_.truncate();

  return tombstone_count;
}

StatusInfo StorageEngine::status() const {
  StatusInfo info;
  info.page_count = pages_.size();
  info.disk_usage = pages_.size() * kPageSize;

  for (uint32_t pid = 0; pid < pages_.size(); ++pid) {
    auto* page = pages_[pid];
    for (uint16_t i = 0; i < page->record_count(); ++i) {
      auto rec = page->read_record(i);
      if (rec) {
        if (rec->tombstone)
          info.tombstone_count++;
        else
          info.live_count++;
      }
    }
  }

  return info;
}

} // namespace silo::storage
