#pragma once

#include <cstdlib>
#include <ctime>
#include <functional>
#include <string>
#include <vector>
#include <cstdio>

inline std::string test_dir(const char* name) {
  const char* tmp = std::getenv("TMPDIR");
  if (!tmp) tmp = std::getenv("TMP");
  if (!tmp) tmp = std::getenv("TEMPDIR");
  if (!tmp) tmp = "/tmp";
  return std::string(tmp) + "/silo_" + name + "_" + std::to_string(std::time(nullptr));
}

inline std::vector<std::pair<std::string, std::function<bool()>>>& tests() {
  static std::vector<std::pair<std::string, std::function<bool()>>> t;
  return t;
}

struct TestRegistrar {
  TestRegistrar(const char* name, std::function<bool()> fn) {
    tests().push_back({name, std::move(fn)});
  }
};

#define CONCAT_(a, b) a##b
#define CONCAT(a, b) CONCAT_(a, b)

#define TEST_CASE(name)                                                  \
  static bool CONCAT(_test_impl_, __LINE__)();                            \
  static TestRegistrar CONCAT(_test_reg_, __LINE__)(name, CONCAT(_test_impl_, __LINE__)); \
  static bool CONCAT(_test_impl_, __LINE__)()
