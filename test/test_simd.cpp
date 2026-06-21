#include "test_main.h"
#include "../src/query/simd.h"

#include <cmath>
#include <cstdlib>
#include <vector>

TEST_CASE("SIMD matches scalar for dim 8") {
  float a[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
  float b[8] = {8.0f, 7.0f, 6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f};
  float simd = silo::query::dot_product_simd(a, b, 8);
  float scalar = silo::query::dot_product_scalar(a, b, 8);
  return std::abs(simd - scalar) < 1e-5f;
}

TEST_CASE("SIMD matches scalar for dim 16") {
  float a[16], b[16];
  for (int i = 0; i < 16; ++i) {
    a[i] = float(i);
    b[i] = float(15 - i);
  }
  float simd = silo::query::dot_product_simd(a, b, 16);
  float scalar = silo::query::dot_product_scalar(a, b, 16);
  return std::abs(simd - scalar) < 1e-5f;
}

TEST_CASE("SIMD matches scalar for dim 32") {
  float a[32], b[32];
  for (int i = 0; i < 32; ++i) {
    a[i] = float(i * 2);
    b[i] = float(i * 3);
  }
  float simd = silo::query::dot_product_simd(a, b, 32);
  float scalar = silo::query::dot_product_scalar(a, b, 32);
  return std::abs(simd - scalar) < 1e-5f;
}

TEST_CASE("SIMD matches scalar for dim 64") {
  float a[64], b[64];
  for (int i = 0; i < 64; ++i) {
    a[i] = float(i * i);
    b[i] = float(63 - i);
  }
  float simd = silo::query::dot_product_simd(a, b, 64);
  float scalar = silo::query::dot_product_scalar(a, b, 64);
  return std::abs(simd - scalar) < 1e-5f;
}
