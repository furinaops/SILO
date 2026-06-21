#pragma once

#include "../crypto/sic.h"

#include <cstdint>
#include <string>
#include <vector>

namespace silo::storage {

struct Record {
  std::string id;
  crypto::SIC sic{};
  uint64_t timestamp = 0;
  uint64_t dimension = 0;
  std::vector<float> vector;
  bool tombstone = false;
};

} // namespace silo::storage
