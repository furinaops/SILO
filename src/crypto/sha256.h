#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace silo::crypto {

class SHA256 {
 public:
  static constexpr size_t kDigestSize = 32;

  SHA256();

  void update(const uint8_t* data, size_t len);
  std::array<uint8_t, kDigestSize> finalize();

  static std::array<uint8_t, kDigestSize> hash(const uint8_t* data, size_t len);
  static std::string hex(const std::array<uint8_t, kDigestSize>& digest);

 private:
  void transform(const uint8_t block[64]);

  uint64_t bit_count_ = 0;
  uint8_t buffer_[64] = {};
  size_t buffer_len_ = 0;
  uint32_t state_[8] = {};
};

} // namespace silo::crypto
