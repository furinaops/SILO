#pragma once

#include <cstddef>
#include <vector>

namespace silo::query {

float dot_product_simd(const float* a, const float* b, size_t dim);
float dot_product_scalar(const float* a, const float* b, size_t dim);

float cosine_similarity(const float* a, const float* b, size_t dim);
float euclidean_distance(const float* a, const float* b, size_t dim);

} // namespace silo::query
