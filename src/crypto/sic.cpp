#include "sic.h"
#include "../storage/record.h"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace silo::crypto {

SIC SICUtils::generate(const std::string& id, uint64_t dimension,
                       const uint8_t* vector_bytes, size_t vector_len,
                       uint64_t timestamp) {
  std::string input;
  input += id;
  input += '|';
  input += std::to_string(dimension);
  input += '|';
  input.append(reinterpret_cast<const char*>(vector_bytes), vector_len);
  input += '|';
  input += std::to_string(timestamp);

  return SHA256::hash(reinterpret_cast<const uint8_t*>(input.data()), input.size());
}

SIC SICUtils::generate(const storage::Record& record) {
  return generate(
      record.id,
      record.vector.size(),
      reinterpret_cast<const uint8_t*>(record.vector.data()),
      record.vector.size() * sizeof(float),
      record.timestamp);
}

bool SICUtils::verify(const storage::Record& record, const SIC& expected) {
  return generate(record) == expected;
}

std::string SICUtils::to_string(const SIC& sic) {
  return SHA256::hex(sic);
}

} // namespace silo::crypto
