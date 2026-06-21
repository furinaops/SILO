#pragma once

#include "../crypto/sic.h"
#include "record.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace silo::storage {

enum class WalEntryType : uint8_t {
  INSERT = 0,
  DELETE = 1,
  COMPACT = 2,
};

struct WalEntry {
  WalEntryType type;
  uint64_t timestamp;
  crypto::SIC sic;
  std::vector<float> vector;
  std::string id;
};

static constexpr size_t kWalHeaderSize = 1 + 8 + 32;
static constexpr size_t kWalBufferSize = 65536;

class WAL {
 public:
  WAL() = default;
  ~WAL();

  bool open(const std::string& db_dir);
  void close();
  void append(const WalEntry& entry);
  void flush();
  std::vector<WalEntry> read_all();
  void truncate();

 private:
  std::string current_path_;
  std::fstream file_;
  size_t segment_num_ = 0;
  std::vector<uint8_t> buffer_;
  std::chrono::steady_clock::time_point last_flush_;

  void buffer_write(const uint8_t* data, size_t len);
  std::string next_path(const std::string& db_dir);
};

} // namespace silo::storage
