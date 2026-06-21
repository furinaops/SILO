#pragma once

#include <cstdlib>
#include <string>

namespace silo::util {

struct Config {
  static bool has_avx2() {
#ifdef __AVX2__
    return true;
#else
    return false;
#endif
  }

  static size_t page_size() { return 8192; }
  static size_t wal_buffer_size() { return 65536; }
  static size_t max_dim() { return 4096; }
  static size_t min_dim() { return 8; }

  static std::string version() { return "0.1.0"; }
};

} // namespace silo::util
