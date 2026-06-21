#pragma once

#include "sha256.h"

#include <array>
#include <cstdint>
#include <string>

namespace silo::storage {
struct Record;
}

namespace silo::crypto {

using SIC = std::array<uint8_t, 32>;

struct SICUtils {
  static SIC generate(const std::string& id, uint64_t dimension,
                      const uint8_t* vector_bytes, size_t vector_len,
                      uint64_t timestamp);
  static SIC generate(const storage::Record& record);
  static bool verify(const storage::Record& record, const SIC& expected);
  static std::string to_string(const SIC& sic);
};

} // namespace silo::crypto

namespace std {

template <>
struct hash<silo::crypto::SIC> {
  size_t operator()(const silo::crypto::SIC& sic) const noexcept {
    size_t h = 0;
    for (size_t i = 0; i < 32; ++i) {
      h = h * 131 + sic[i];
    }
    return h;
  }
};

} // namespace std
