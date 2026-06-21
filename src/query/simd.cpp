#include "simd.h"

#include <cmath>
#include <cstddef>

namespace silo::query {

#ifdef __AVX2__
#include <immintrin.h>

float dot_product_simd(const float* a, const float* b, size_t dim) {
  __m256 sum = _mm256_setzero_ps();
  size_t i = 0;
  for (; i + 8 <= dim; i += 8) {
    __m256 va = _mm256_loadu_ps(a + i);
    __m256 vb = _mm256_loadu_ps(b + i);
    sum = _mm256_fmadd_ps(va, vb, sum);
  }
  float result = 0.0f;
  alignas(32) float temp[8];
  _mm256_store_ps(temp, sum);
  for (int j = 0; j < 8; ++j) result += temp[j];
  for (; i < dim; ++i) result += a[i] * b[i];
  return result;
}

#else

float dot_product_simd(const float* a, const float* b, size_t dim) {
  return dot_product_scalar(a, b, dim);
}

#endif

float dot_product_scalar(const float* a, const float* b, size_t dim) {
  float result = 0.0f;
  for (size_t i = 0; i < dim; ++i) {
    result += a[i] * b[i];
  }
  return result;
}

float cosine_similarity(const float* a, const float* b, size_t dim) {
  float dot = dot_product_simd(a, b, dim);
  float norm_a = 0.0f, norm_b = 0.0f;
  for (size_t i = 0; i < dim; ++i) {
    norm_a += a[i] * a[i];
    norm_b += b[i] * b[i];
  }
  float denom = std::sqrt(norm_a) * std::sqrt(norm_b);
  if (denom < 1e-10f) return 0.0f;
  return dot / denom;
}

float euclidean_distance(const float* a, const float* b, size_t dim) {
  float sum = 0.0f;
  for (size_t i = 0; i < dim; ++i) {
    float diff = a[i] - b[i];
    sum += diff * diff;
  }
  return std::sqrt(sum);
}

} // namespace silo::query
