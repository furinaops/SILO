#include "page.h"
#include "../crypto/sha256.h"

#include <cstring>

namespace silo::storage {

PageHeader& Page::header() {
  return *reinterpret_cast<PageHeader*>(data_);
}

const PageHeader& Page::header() const {
  return *reinterpret_cast<const PageHeader*>(data_);
}

uint8_t* Page::payload() {
  return data_ + kPageHeaderSize;
}

const uint8_t* Page::payload() const {
  return data_ + kPageHeaderSize;
}

uint32_t Page::compute_checksum(const uint8_t* data, size_t len) {
  auto hash = crypto::SHA256::hash(data, len);
  uint32_t result = 0;
  for (size_t i = 0; i < 4; ++i) {
    result = (result << 8) | hash[i];
  }
  return result;
}

uint32_t Page::compute_checksum() const {
  return compute_checksum(data_ + kPageHeaderSize, kPageSize - kPageHeaderSize);
}

bool Page::verify_checksum() const {
  return header().checksum == compute_checksum();
}

size_t Page::record_offset(size_t index) const {
  if (index > header().record_count) return kPagePayloadSize;
  size_t offset = 0;
  for (uint16_t i = 0; i < index; ++i) {
    uint16_t len;
    std::memcpy(&len, payload() + offset, sizeof(len));
    offset += len;
  }
  return offset;
}

size_t Page::available_bytes() const {
  size_t offset = record_offset(header().record_count);
  return kPagePayloadSize - offset;
}

bool Page::can_fit(size_t record_bytes) const {
  return available_bytes() >= record_bytes;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wrestrict"

bool Page::write_record(const Record& record, const crypto::SIC& sic) {
  size_t id_bytes = record.id.size();
  size_t vec_bytes = record.vector.size() * sizeof(float);
  size_t rec_size = sizeof(RecordOnDisk) + id_bytes + vec_bytes;
  if (rec_size > available_bytes()) return false;

  size_t offset = record_offset(header().record_count);

  RecordOnDisk rod;
  rod.length = static_cast<uint16_t>(rec_size);
  rod.flags = record.tombstone ? 1 : 0;
  rod.timestamp = record.timestamp;
  rod.id_len = static_cast<uint32_t>(id_bytes);
  rod.dim = static_cast<uint32_t>(record.vector.size());
  rod.sic = sic;

  std::memcpy(payload() + offset, &rod, sizeof(rod));
  offset += sizeof(rod);
  if (id_bytes > 0) {
    std::memcpy(payload() + offset, record.id.data(), id_bytes);
    offset += id_bytes;
  }
  if (vec_bytes > 0) {
    std::memcpy(payload() + offset, record.vector.data(), vec_bytes);
  }

  header().record_count++;
  header().checksum = compute_checksum();
  return true;
}
#pragma GCC diagnostic pop

std::optional<Record> Page::read_record(size_t index) const {
  if (index >= header().record_count) return std::nullopt;

  size_t offset = record_offset(index);
  RecordOnDisk rod;
  std::memcpy(&rod, payload() + offset, sizeof(rod));
  offset += sizeof(rod);

  Record rec;
  rec.timestamp = rod.timestamp;
  rec.tombstone = (rod.flags & 1) != 0;
  rec.dimension = rod.dim;
  rec.sic = rod.sic;

  rec.id.assign(reinterpret_cast<const char*>(payload() + offset), rod.id_len);
  offset += rod.id_len;

  rec.vector.resize(rod.dim);
  if (rod.dim > 0) {
    std::memcpy(rec.vector.data(), payload() + offset, rod.dim * sizeof(float));
  }

  return rec;
}

bool Page::mark_tombstone(size_t index) {
  if (index >= header().record_count) return false;
  size_t offset = record_offset(index);
  // flags is at offset + 2 (after uint16_t length)
  uint16_t flags;
  std::memcpy(&flags, payload() + offset + 2, sizeof(flags));
  flags |= 1; // set tombstone bit
  std::memcpy(payload() + offset + 2, &flags, sizeof(flags));
  header().checksum = compute_checksum();
  return true;
}

size_t Page::record_count() const {
  return header().record_count;
}

void Page::clear() {
  std::memset(data_, 0, kPageSize);
}

} // namespace silo::storage
