#include "test_main.h"

#include <cstdio>

int main() {
  int passed = 0, failed = 0;
  for (auto& [name, fn] : tests()) {
    if (fn()) {
      std::printf("  PASS  %s\n", name.c_str());
      ++passed;
    } else {
      std::printf("  FAIL  %s\n", name.c_str());
      ++failed;
    }
  }
  std::printf("\n%d passed, %d failed\n", passed, failed);
  return failed > 0 ? 1 : 0;
}
