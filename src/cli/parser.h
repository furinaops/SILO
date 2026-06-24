#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace silo::cli {

enum class Command {
  NONE,
  INSERT,
  SEARCH,
  DELETE,
  FETCH,
  COMPACT,
  STATUS,
  HELP,
  EXIT,
  BUILD_CASCADE,
};

struct ParsedCommand {
  Command cmd = Command::NONE;
  std::unordered_map<std::string, std::string> args;
  bool json = false;
  std::string raw;
};

ParsedCommand parse(const std::string& line);
std::vector<float> parse_vector(const std::string& s);

} // namespace silo::cli
