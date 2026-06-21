#include "wal.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace silo::storage {

namespace {

bool starts_with(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

WAL::~WAL() { flush(); close(); }

bool WAL::open(const std::string& db_dir) {
  std::filesystem::create_directories(db_dir);
  segment_num_ = 0;
  for (const auto& entry : std::filesystem::directory_iterator(db_dir)) {
    auto name = entry.path().filename().string();
    if (starts_with(name, "wal-") && ends_with(name, ".log")) {
      auto num_str = name.substr(4, name.size() - 8);
      try {
        size_t num = std::stoull(num_str);
        if (num >= segment_num_) segment_num_ = num + 1;
      } catch (...) {}
    }
  }
  current_path_ = db_dir + "/wal-" + std::to_string(segment_num_) + ".log";
  file_.open(current_path_, std::ios::binary | std::ios::app | std::ios::out | std::ios::in);
  if (!file_.is_open()) {
    file_.clear();
    file_.open(current_path_, std::ios::binary | std::ios::app | std::ios::out);
    if (!file_.is_open()) return false;
  }
  buffer_.reserve(kWalBufferSize);
  last_flush_ = std::chrono::steady_clock::now();
  return true;
}

void WAL::close() {
  if (file_.is_open()) file_.close();
}

void WAL::buffer_write(const uint8_t* data, size_t len) {
  buffer_.insert(buffer_.end(), data, data + len);
  if (buffer_.size() >= kWalBufferSize) {
    flush();
  }
  auto now = std::chrono::steady_clock::now();
  if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_flush_).count() >= 10) {
    flush();
  }
}

void WAL::flush() {
  if (!file_.is_open() || buffer_.empty()) return;
  file_.write(reinterpret_cast<const char*>(buffer_.data()), buffer_.size());
  file_.flush();
  buffer_.clear();
  last_flush_ = std::chrono::steady_clock::now();
}

void WAL::append(const WalEntry& entry) {
  if (!file_.is_open()) return;

  size_t id_len = entry.id.size();
  size_t vec_bytes = entry.vector.size() * sizeof(float);
  size_t payload_size = 1 + 8 + 32 + id_len + vec_bytes;

  uint32_t len = static_cast<uint32_t>(payload_size);
  buffer_write(reinterpret_cast<const uint8_t*>(&len), 4);

  uint8_t type_byte = static_cast<uint8_t>(entry.type);
  buffer_write(&type_byte, 1);

  buffer_write(reinterpret_cast<const uint8_t*>(&entry.timestamp), 8);
  buffer_write(entry.sic.data(), 32);

  if (id_len > 0) {
    buffer_write(reinterpret_cast<const uint8_t*>(entry.id.data()), id_len);
  }
  if (vec_bytes > 0) {
    buffer_write(reinterpret_cast<const uint8_t*>(entry.vector.data()), vec_bytes);
  }
}

std::vector<WalEntry> WAL::read_all() {
  flush();
  std::vector<WalEntry> entries;
  close();

  std::string db_dir = std::filesystem::path(current_path_).parent_path().string();

  std::vector<std::string> wal_files;
  for (const auto& entry : std::filesystem::directory_iterator(db_dir)) {
    auto name = entry.path().filename().string();
    if (starts_with(name, "wal-") && ends_with(name, ".log")) {
      wal_files.push_back(entry.path().string());
    }
  }
  std::sort(wal_files.begin(), wal_files.end());

  for (const auto& path : wal_files) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) continue;

    while (file) {
      uint32_t entry_len;
      file.read(reinterpret_cast<char*>(&entry_len), 4);
      if (!file || entry_len == 0) break;

      std::vector<uint8_t> buf(entry_len);
      file.read(reinterpret_cast<char*>(buf.data()), entry_len);
      if (!file) break;

      WalEntry we;
      size_t offset = 0;
      we.type = static_cast<WalEntryType>(buf[offset]);
      offset += 1;

      std::memcpy(&we.timestamp, buf.data() + offset, 8);
      offset += 8;

      crypto::SIC sic;
      std::memcpy(sic.data(), buf.data() + offset, 32);
      we.sic = sic;
      offset += 32;

      if (we.type == WalEntryType::INSERT) {
        size_t remaining = entry_len - offset;
        size_t id_len = 0;
        for (size_t i = offset; i < entry_len; ++i) {
          if (buf[i] == 0) { id_len = i - offset; break; }
        }
        if (id_len == 0 || id_len > remaining) id_len = remaining;
        we.id.assign(reinterpret_cast<const char*>(buf.data() + offset), id_len);
        offset += id_len;

        size_t vec_bytes = remaining - id_len;
        if (vec_bytes > 0 && vec_bytes % sizeof(float) == 0) {
          we.vector.resize(vec_bytes / sizeof(float));
          std::memcpy(we.vector.data(), buf.data() + offset, vec_bytes);
        }
      } else if (we.type == WalEntryType::DELETE) {
        size_t remaining = entry_len - offset;
        we.id.assign(reinterpret_cast<const char*>(buf.data() + offset), remaining);
      }

      entries.push_back(std::move(we));
    }
  }

  open(db_dir);
  return entries;
}

void WAL::truncate() {
  flush();
  close();
  std::string db_dir = std::filesystem::path(current_path_).parent_path().string();
  for (const auto& entry : std::filesystem::directory_iterator(db_dir)) {
    auto name = entry.path().filename().string();
    if (starts_with(name, "wal-") && ends_with(name, ".log")) {
      std::filesystem::remove(entry.path());
    }
  }
  segment_num_ = 0;
  open(db_dir);
}

std::string WAL::next_path(const std::string& db_dir) {
  return db_dir + "/wal-" + std::to_string(segment_num_++) + ".log";
}

} // namespace silo::storage
