#include "parser.h"

#include <charconv>
#include <cstddef>
#include <sstream>
#include <stdexcept>

namespace silo::cli {

namespace {

std::string unquote(const std::string& s) {
  if (s.size() < 2) return s;
  char q = s.front();
  if ((q == '"' || q == '\'') && s.back() == q) {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

} // namespace

ParsedCommand parse(const std::string& line) {
  ParsedCommand result;
  result.raw = line;

  if (line.empty() || line[0] != '/') return result;

  std::istringstream iss(line);
  std::string token;
  iss >> token;

  if (token == "/insert") result.cmd = Command::INSERT;
  else if (token == "/search") result.cmd = Command::SEARCH;
  else if (token == "/delete") result.cmd = Command::DELETE;
  else if (token == "/fetch") result.cmd = Command::FETCH;
  else if (token == "/compact") result.cmd = Command::COMPACT;
  else if (token == "/status") result.cmd = Command::STATUS;
  else if (token == "/help") result.cmd = Command::HELP;
  else if (token == "/exit") result.cmd = Command::EXIT;

  std::string key;
  while (iss >> token) {
    if (token == "--json") {
      result.json = true;
    } else if (token.size() > 2 && token[0] == '-' && token[1] == '-') {
      key = token.substr(2);
    } else if (!key.empty()) {
      std::string val = token;
      char q = val.front();
      if ((q == '"' || q == '\'') && val.back() != q) {
        std::string rest;
        while (iss >> rest) {
          val += " " + rest;
          if (rest.back() == q) break;
        }
      }
      result.args[key] = unquote(val);
      key.clear();
    }
  }

  auto it = result.args.find("vec");
  if (it != result.args.end()) {
    std::string& val = it->second;
    if (val.front() == '[' && val.back() != ']') {
      std::string rest;
      while (iss >> rest) {
        val += " " + rest;
        if (rest.back() == ']') break;
      }
    }
  }

  return result;
}

std::vector<float> parse_vector(const std::string& s) {
  std::vector<float> result;

  size_t start = s.find('[');
  size_t end = s.find(']');
  if (start == std::string::npos || end == std::string::npos) {
    throw std::invalid_argument("Vector must be in format [x, y, z, ...]");
  }

  std::string content = s.substr(start + 1, end - start - 1);
  std::istringstream ss(content);
  std::string token;

  while (std::getline(ss, token, ',')) {
    while (!token.empty() && token.front() == ' ') token.erase(0, 1);
    while (!token.empty() && token.back() == ' ') token.pop_back();
    if (token.empty()) continue;

    float val;
    auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), val);
    if (ec != std::errc()) {
      throw std::invalid_argument("Invalid float in vector: " + token);
    }
    result.push_back(val);
  }

  return result;
}

} // namespace silo::cli
