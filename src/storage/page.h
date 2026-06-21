#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "record.h"

namespace silo::storage {

static constexpr size_t kPageSize = 8192;
static constexpr size_t kPageHeaderSize = 12;
static constexpr size_t kPagePayloadSize = kPageSize - kPageHeaderSize;

struct PageHeader {
  uint32_t page_id;
  uint32_t checksum;
  uint16_t record_count;
  uint16_t flags;
} __attribute__((packed));

static_assert(sizeof(PageHeader) == kPageHeaderSize, "PageHeader must be 12 bytes");

struct RecordOnDisk {
  uint16_t length;
  uint16_t flags;
  uint64_t timestamp;
  uint32_t id_len;
  uint32_t dim;
  crypto::SIC sic;
  // followed by id bytes, then vector bytes
} __attribute__((packed));

class Page {
 public:
  uint8_t data_[kPageSize];

  PageHeader& header();
  const PageHeader& header() const;

  uint8_t* payload();
  const uint8_t* payload() const;

  static uint32_t compute_checksum(const uint8_t* data, size_t len);
  uint32_t compute_checksum() const;

  bool verify_checksum() const;

  size_t record_offset(size_t index) const;
  size_t available_bytes() const;
  bool can_fit(size_t record_bytes) const;

  bool write_record(const Record& record, const crypto::SIC& sic);
  std::optional<Record> read_record(size_t index) const;
  bool mark_tombstone(size_t index);
  size_t record_count() const;

  void clear();
};

} // namespace silo::storage
