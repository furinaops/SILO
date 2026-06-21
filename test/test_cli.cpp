#include "test_main.h"
#include "../src/cli/parser.h"

#include <string>

TEST_CASE("Parse /insert command") {
  auto cmd = silo::cli::parse("/insert --id foo --vec [1,2,3]");
  return cmd.cmd == silo::cli::Command::INSERT;
}

TEST_CASE("Parse /search command") {
  auto cmd = silo::cli::parse("/search --vec [1,2,3] --top 5");
  return cmd.cmd == silo::cli::Command::SEARCH;
}

TEST_CASE("Parse /delete command") {
  auto cmd = silo::cli::parse("/delete --sic abc123");
  return cmd.cmd == silo::cli::Command::DELETE;
}

TEST_CASE("Parse /fetch command") {
  auto cmd = silo::cli::parse("/fetch --sic def456");
  return cmd.cmd == silo::cli::Command::FETCH;
}

TEST_CASE("Parse /compact") {
  auto cmd = silo::cli::parse("/compact");
  return cmd.cmd == silo::cli::Command::COMPACT;
}

TEST_CASE("Parse /status") {
  auto cmd = silo::cli::parse("/status");
  return cmd.cmd == silo::cli::Command::STATUS;
}

TEST_CASE("Parse /help") {
  auto cmd = silo::cli::parse("/help");
  return cmd.cmd == silo::cli::Command::HELP;
}

TEST_CASE("Parse /exit") {
  auto cmd = silo::cli::parse("/exit");
  return cmd.cmd == silo::cli::Command::EXIT;
}

TEST_CASE("Parse unknown command returns NONE") {
  auto cmd = silo::cli::parse("/unknown");
  return cmd.cmd == silo::cli::Command::NONE;
}

TEST_CASE("Parse missing leading slash returns NONE") {
  auto cmd = silo::cli::parse("hello");
  return cmd.cmd == silo::cli::Command::NONE;
}

TEST_CASE("Parse --key value pairs") {
  auto cmd = silo::cli::parse("/insert --id myid --vec [1,2]");
  return cmd.args.count("id") && cmd.args.at("id") == "myid" &&
         cmd.args.count("vec") && cmd.args.at("vec") == "[1,2]";
}

TEST_CASE("Parse vector [1,2,3]") {
  auto vec = silo::cli::parse_vector("[1,2,3]");
  return vec.size() == 3 && vec[0] == 1.0f && vec[1] == 2.0f && vec[2] == 3.0f;
}

TEST_CASE("Parse vector with spaces") {
  auto vec = silo::cli::parse_vector("[1.5, 2.5, 3.5]");
  return vec.size() == 3 && vec[0] == 1.5f && vec[1] == 2.5f && vec[2] == 3.5f;
}

TEST_CASE("Parse --json flag in command") {
  auto cmd = silo::cli::parse("/status --json");
  return cmd.cmd == silo::cli::Command::STATUS && cmd.json;
}

TEST_CASE("Parse --json with search") {
  auto cmd = silo::cli::parse("/search --vec [1,2,3] --top 5 --json");
  return cmd.cmd == silo::cli::Command::SEARCH && cmd.json;
}

TEST_CASE("Parse --json with insert") {
  auto cmd = silo::cli::parse("/insert --id x --vec [1,2,3] --json");
  return cmd.cmd == silo::cli::Command::INSERT && cmd.json;
}
