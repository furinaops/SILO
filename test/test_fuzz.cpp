#include "test_main.h"
#include "../src/cli/parser.h"

#include <random>
#include <string>
#include <vector>

TEST_CASE("Fuzz CLI parser with random inputs") {
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> len_dist(0, 128);
  std::uniform_int_distribution<int> char_dist(0, 255);

  for (int iter = 0; iter < 10000; ++iter) {
    int len = len_dist(rng);
    std::string input;
    input.reserve(len);
    for (int i = 0; i < len; ++i) {
      input.push_back(static_cast<char>(char_dist(rng)));
    }

    try {
      auto cmd = silo::cli::parse(input);
      (void)cmd;
    } catch (...) {
      // Parser should never throw
      return false;
    }
  }
  return true;
}

TEST_CASE("Fuzz CLI parser with partial slash commands") {
  std::mt19937 rng(123);
  const char* prefixes[] = {
    "/insert ", "/search ", "/delete ", "/fetch ",
    "/compact ", "/status ", "/help ", "/exit ",
  };
  std::uniform_int_distribution<int> prefix_dist(0, 7);
  std::uniform_int_distribution<int> extra_len(0, 64);
  std::uniform_int_distribution<int> byte_dist(0, 255);

  for (int iter = 0; iter < 10000; ++iter) {
    std::string input = prefixes[prefix_dist(rng)];
    int extra = extra_len(rng);
    for (int i = 0; i < extra; ++i) {
      input.push_back(static_cast<char>(byte_dist(rng)));
    }

    try {
      auto cmd = silo::cli::parse(input);
      (void)cmd;
    } catch (...) {
      return false;
    }
  }
  return true;
}

TEST_CASE("Fuzz parse_vector with random bracket strings") {
  std::mt19937 rng(456);
  std::uniform_int_distribution<int> len_dist(0, 64);
  std::uniform_int_distribution<int> char_dist(0, 255);

  for (int iter = 0; iter < 10000; ++iter) {
    int len = len_dist(rng);
    std::string input = "[";
    for (int i = 0; i < len; ++i) {
      input.push_back(static_cast<char>(char_dist(rng)));
    }
    input.push_back(']');

    try {
      auto vec = silo::cli::parse_vector(input);
      (void)vec;
    } catch (const std::invalid_argument&) {
      // Expected for malformed inputs
      continue;
    } catch (...) {
      return false;
    }
  }
  return true;
}
