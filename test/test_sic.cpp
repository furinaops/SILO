#include "test_main.h"
#include "../src/crypto/sha256.h"
#include "../src/crypto/sic.h"
#include "../src/storage/record.h"

#include <cstring>
#include <string>

using namespace silo::crypto;
using namespace silo::storage;

TEST_CASE("SHA256 empty string") {
  auto h = SHA256::hash(reinterpret_cast<const uint8_t*>(""), 0);
  return SHA256::hex(h) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
}

TEST_CASE("SHA256 abc") {
  auto h = SHA256::hash(reinterpret_cast<const uint8_t*>("abc"), 3);
  return SHA256::hex(h) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
}

TEST_CASE("SHA256 abcdbcde... pattern") {
  std::string input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  auto h = SHA256::hash(reinterpret_cast<const uint8_t*>(input.data()), input.size());
  return SHA256::hex(h) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
}

TEST_CASE("SHA256 large input") {
  std::string input(1000, 'a');
  auto h = SHA256::hash(reinterpret_cast<const uint8_t*>(input.data()), input.size());
  return SHA256::hex(h) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3";
}

TEST_CASE("SIC generate from record") {
  Record rec;
  rec.id = "test1";
  rec.timestamp = 1234567890;
  rec.vector = {1.0f, 2.0f, 3.0f, 4.0f};

  auto sic = SICUtils::generate(rec);
  auto sic2 = SICUtils::generate(rec);
  return sic == sic2;
}

TEST_CASE("SIC verify passes for unmodified record") {
  Record rec;
  rec.id = "verify-test";
  rec.timestamp = 42;
  rec.vector = {0.5f, 0.25f, 0.125f};

  auto sic = SICUtils::generate(rec);
  return SICUtils::verify(rec, sic);
}

TEST_CASE("SIC verify fails after id mutation") {
  Record rec;
  rec.id = "original";
  rec.timestamp = 100;
  rec.vector = {1.0f, 2.0f};

  auto sic = SICUtils::generate(rec);
  rec.id = "tampered";
  return !SICUtils::verify(rec, sic);
}

TEST_CASE("SIC verify fails after vector mutation") {
  Record rec;
  rec.id = "vec-test";
  rec.timestamp = 200;
  rec.vector = {1.0f, 2.0f, 3.0f};

  auto sic = SICUtils::generate(rec);
  rec.vector[1] = 99.0f;
  return !SICUtils::verify(rec, sic);
}

TEST_CASE("SIC verify fails after timestamp mutation") {
  Record rec;
  rec.id = "ts-test";
  rec.timestamp = 111;
  rec.vector = {7.0f, 8.0f};

  auto sic = SICUtils::generate(rec);
  rec.timestamp = 999;
  return !SICUtils::verify(rec, sic);
}

TEST_CASE("SIC with empty id") {
  Record rec;
  rec.id = "";
  rec.timestamp = 0;
  rec.vector = {};

  auto sic = SICUtils::generate(rec);
  return SICUtils::verify(rec, sic);
}

TEST_CASE("SIC with null bytes in vector is binary-safe") {
  Record rec;
  rec.id = "binary";
  rec.timestamp = 1;
  rec.vector = {0.0f, -0.0f, 3.14f};

  auto sic = SICUtils::generate(rec);
  return SICUtils::verify(rec, sic);
}

TEST_CASE("SIC to_string produces 64-char hex") {
  Record rec;
  rec.id = "hex-test";
  rec.timestamp = 999;
  rec.vector = {1.0f};

  auto sic = SICUtils::generate(rec);
  auto str = SICUtils::to_string(sic);
  return str.size() == 64;
}

TEST_CASE("Different records produce different SICs") {
  Record rec1, rec2;
  rec1.id = "a";
  rec1.timestamp = 1;
  rec1.vector = {1.0f};

  rec2.id = "b";
  rec2.timestamp = 2;
  rec2.vector = {2.0f};

  auto sic1 = SICUtils::generate(rec1);
  auto sic2 = SICUtils::generate(rec2);
  return sic1 != sic2;
}
