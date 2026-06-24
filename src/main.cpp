#include "cli/parser.h"
#include "crypto/sha256.h"
#include "crypto/sic.h"
#include "query/engine.h"
#include "storage/engine.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

const char* kGreen = "\033[32m";
const char* kRed = "\033[31m";
const char* kReset = "\033[0m";

bool no_color() {
  static bool check = (std::getenv("NO_COLOR") != nullptr);
  return check;
}

void print_green(const std::string& msg) {
  if (no_color()) std::cout << msg;
  else std::cout << kGreen << msg << kReset;
}

void print_red(const std::string& msg) {
  if (no_color()) std::cout << msg;
  else std::cout << kRed << msg << kReset;
}

void print_help() {
  std::cout <<
    "SILO v0.1.0 - Vector Database\n"
    "Commands:\n"
    "  /insert --id <str> --vec [x,y,...]   Insert a vector\n"
    "  /search --vec [x,y,...] --top <n>     Search top-K similar vectors\n"
    "  /delete --sic <hex>                   Delete a vector by SIC\n"
    "  /fetch --sic <hex>                    Fetch a vector by SIC\n"
    "  /compact                              Purge tombstoned records\n"
    "  /build-cascade                        Build CASCADE approximate index\n"
  "  /search --vec [x,y,...] --top <n> [--algo cascade] [--trees <N>|auto|all] [--probe <N>]\n"
  "                                        Search top-K (brute-force or cascade;\n"
  "                                        --trees: number of trees to probe (default 3, auto, all)\n"
  "                                        --probe: beam width for multi-probe descent (1=greedy, default=3)\n"
    "  /status                               Show database status\n"
    "  /help                                 Show this help\n"
    "  /exit                                 Exit SILO\n"
    "  Append --json to any command for machine-readable output.\n";
}

std::string json_escape(const std::string& s) {
  std::ostringstream out;
  for (char c : s) {
    if (c == '"' || c == '\\') out << '\\' << c;
    else if (c == '\n') out << "\\n";
    else if (c == '\t') out << "\\t";
    else out << c;
  }
  return out.str();
}

void print_json_insert(const std::string& sic_hex) {
  std::cout << "{\"status\":\"ok\",\"action\":\"insert\",\"sic\":\"" << sic_hex << "\"}\n";
}

void print_json_search(const std::vector<silo::query::SearchResult>& results) {
  std::cout << "[";
  for (size_t i = 0; i < results.size(); ++i) {
    if (i > 0) std::cout << ",";
    std::cout << "{\"rank\":" << (i + 1)
              << ",\"id\":\"" << json_escape(results[i].id)
              << "\",\"sic\":\"" << results[i].sic_hex
              << "\",\"score\":" << results[i].score << "}";
  }
  std::cout << "]\n";
}

void print_json_delete(bool found, const std::string& sic_hex) {
  if (found) {
    std::cout << "{\"status\":\"ok\",\"action\":\"delete\",\"sic\":\"" << sic_hex << "\"}\n";
  } else {
    std::cout << "{\"status\":\"error\",\"message\":\"SIC not found\",\"sic\":\"" << sic_hex << "\"}\n";
  }
}

void print_json_fetch(const std::optional<silo::storage::Record>& rec, const std::string& sic_hex) {
  if (rec) {
    std::cout << "{\"status\":\"ok\",\"id\":\"" << json_escape(rec->id)
              << "\",\"dim\":" << rec->dimension
              << ",\"timestamp\":" << rec->timestamp
              << ",\"vec\":[";
    for (size_t i = 0; i < rec->vector.size(); ++i) {
      if (i > 0) std::cout << ",";
      std::cout << rec->vector[i];
    }
    std::cout << "]}\n";
  } else {
    std::cout << "{\"status\":\"error\",\"message\":\"SIC not found\",\"sic\":\"" << sic_hex << "\"}\n";
  }
}

void print_json_compact(size_t freed) {
  std::cout << "{\"status\":\"ok\",\"action\":\"compact\",\"tombstones_purged\":" << freed << "}\n";
}

void print_json_status(const silo::storage::StatusInfo& s) {
  std::cout << "{\"live_vectors\":" << s.live_count
            << ",\"tombstones\":" << s.tombstone_count
            << ",\"pages\":" << s.page_count
            << ",\"disk_bytes\":" << s.disk_usage << "}\n";
}

silo::crypto::SIC parse_sic_hex(const std::string& hex) {
  silo::crypto::SIC sic{};
  for (int i = 0; i < 32; ++i) {
    auto sub = hex.substr(2 * i, 2);
    sic[i] = static_cast<uint8_t>(std::stoul(sub, nullptr, 16));
  }
  return sic;
}

} // namespace

int main(int argc, char* argv[]) {
  std::string db_dir = [] {
    const char* env = std::getenv("SILO_DB_DIR");
    if (env) return std::string(env);
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg) return std::string(xdg) + "/silo";
    const char* home = std::getenv("HOME");
    if (home) return std::string(home) + "/.local/share/silo";
    return std::string("/tmp/silo");
  }();
  if (argc > 1) db_dir = argv[1];

  silo::storage::StorageEngine engine;
  if (!engine.open(db_dir)) {
    engine.create(db_dir);
  }

  silo::query::QueryEngine qe(engine);

  std::cout << "SILO v0.1.0" << std::endl;
  std::cout << "Database: " << db_dir << std::endl;
  std::cout << "Type /help for commands." << std::endl;

  std::string line;
  while (true) {
    std::cout << "> " << std::flush;
    if (!std::getline(std::cin, line)) break;

    auto cmd = silo::cli::parse(line);

    switch (cmd.cmd) {
      case silo::cli::Command::INSERT: {
        auto id_it = cmd.args.find("id");
        auto vec_it = cmd.args.find("vec");
        if (id_it == cmd.args.end() || vec_it == cmd.args.end()) {
          if (cmd.json) {
            std::cout << "{\"status\":\"error\",\"message\":\"Usage: /insert --id <str> --vec [x,y,...]\"}\n";
          } else {
            print_red("[ERROR] Usage: /insert --id <str> --vec [x,y,...]\n");
          }
          break;
        }
        try {
          auto vec = silo::cli::parse_vector(vec_it->second);
          if (vec.size() < 8 || vec.size() > 4096) {
            if (cmd.json) {
              std::cout << "{\"status\":\"error\",\"message\":\"Dimension must be between 8 and 4096\",\"got\":" << vec.size() << "}\n";
            } else {
              print_red("[ERROR] Dimension must be between 8 and 4096 (got: " +
                        std::to_string(vec.size()) + ")\n");
            }
            break;
          }
          silo::storage::Record rec;
          rec.id = id_it->second;
          rec.timestamp = std::time(nullptr);
          rec.vector = vec;
          rec.dimension = vec.size();

          auto sic = silo::crypto::SICUtils::generate(rec);
          rec.sic = sic;

          engine.store(rec, sic);
          auto sic_hex = silo::crypto::SICUtils::to_string(sic);
          if (cmd.json) {
            print_json_insert(sic_hex);
          } else {
            print_green("[OK] Inserted " + sic_hex + "\n");
          }
        } catch (const std::exception& e) {
          if (cmd.json) {
            std::cout << "{\"status\":\"error\",\"message\":\"" << json_escape(e.what()) << "\"}\n";
          } else {
            print_red("[ERROR] " + std::string(e.what()) + "\n");
          }
        }
        break;
      }

      case silo::cli::Command::SEARCH: {
        auto vec_it = cmd.args.find("vec");
        auto top_it = cmd.args.find("top");
        if (vec_it == cmd.args.end()) {
          if (cmd.json) {
            std::cout << "{\"status\":\"error\",\"message\":\"Usage: /search --vec [x,y,...] --top <n> [--algo cascade]\"}\n";
          } else {
            print_red("[ERROR] Usage: /search --vec [x,y,...] --top <n> [--algo cascade]\n");
          }
          break;
        }
        try {
          auto query = silo::cli::parse_vector(vec_it->second);
          int top_k = 10;
          if (top_it != cmd.args.end()) {
            top_k = std::stoi(top_it->second);
            if (top_k < 1) throw std::invalid_argument("top_k must be >= 1");
          }

          auto algo_it = cmd.args.find("algo");
          bool use_cascade = (algo_it != cmd.args.end() && algo_it->second == "cascade");

          auto probe_it = cmd.args.find("probe");
          int beam_width = 3;
          if (probe_it != cmd.args.end()) {
            beam_width = std::stoi(probe_it->second);
            if (beam_width < 1) beam_width = 1;
          }

          int num_trees = 3;
          std::string trees_mode;
          auto trees_it = cmd.args.find("trees");
          if (trees_it != cmd.args.end()) {
            std::string val = trees_it->second;
            if (val == "all") {
              num_trees = 0;
              trees_mode = "all";
            } else if (val == "auto") {
              num_trees = -1;
              trees_mode = "auto";
            } else {
              num_trees = std::stoi(val);
              trees_mode = std::to_string(num_trees);
            }
          }

          std::vector<silo::query::SearchResult> results;
          if (use_cascade) {
            if (!qe.cascade_is_built()) {
              if (cmd.json) {
                std::cout << "{\"status\":\"error\",\"message\":\"No CASCADE index built. Run /build-cascade first. Falling back to brute-force.\"}\n";
              } else {
                print_red("[WARN] No CASCADE index built. Run /build-cascade first. Falling back to brute-force.\n");
              }
              results = qe.search(query, top_k);
            } else {
              auto t0 = std::chrono::steady_clock::now();
              results = qe.search_cascade(query, top_k, num_trees, beam_width);
              double elapsed = std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - t0).count();

              int total_trees = qe.cascade_num_trees();
              int actual = (num_trees == 0) ? total_trees :
                           (num_trees == -1) ? std::max(3, std::min(
                               static_cast<int>(std::sqrt(total_trees)), total_trees)) :
                           std::min(std::max(num_trees, 1), total_trees);

              if (!cmd.json) {
                if (!trees_mode.empty()) {
                  std::cout << "[INFO] Searching " << actual << " trees ("
                            << trees_mode << ", out of " << total_trees
                            << " total). Beam width: " << beam_width << ".\n";
                } else {
                  std::cout << "[INFO] Searching " << actual << " trees (out of "
                            << total_trees << " total). Beam width: " << beam_width << ".\n";
                }
                std::cout << "[INFO] Search completed in "
                          << std::fixed << std::setprecision(2) << elapsed << " ms.\n";
              }
            }
          } else {
            results = qe.search(query, top_k);
          }

          if (cmd.json) {
            print_json_search(results);
          } else {
            for (size_t i = 0; i < results.size(); ++i) {
              std::cout << "  " << (i + 1) << ". " << results[i].id
                        << " (sic=" << results[i].sic_hex
                        << ", score=" << results[i].score << ")\n";
            }
          }
        } catch (const std::exception& e) {
          if (cmd.json) {
            std::cout << "{\"status\":\"error\",\"message\":\"" << json_escape(e.what()) << "\"}\n";
          } else {
            print_red("[ERROR] " + std::string(e.what()) + "\n");
          }
        }
        break;
      }

      case silo::cli::Command::BUILD_CASCADE: {
        try {
          qe.build_cascade();
          int n = qe.cascade_num_trees();
          int total = qe.cascade_total_vectors();
          if (cmd.json) {
            std::cout << "{\"status\":\"ok\",\"action\":\"build-cascade\",\"trees\":" << n
                      << ",\"vectors\":" << total << "}\n";
          } else {
            if (n == 0) {
              print_red("[WARN] No vectors to index. Insert some vectors first.\n");
            } else {
              print_green("[OK] Built CASCADE index: " + std::to_string(n) +
                          " trees, " + std::to_string(total) + " vectors\n");
            }
          }
        } catch (const std::exception& e) {
          if (cmd.json) {
            std::cout << "{\"status\":\"error\",\"message\":\"" << json_escape(e.what()) << "\"}\n";
          } else {
            print_red("[ERROR] " + std::string(e.what()) + "\n");
          }
        }
        break;
      }

      case silo::cli::Command::DELETE: {
        auto sic_it = cmd.args.find("sic");
        if (sic_it == cmd.args.end()) {
          if (cmd.json) {
            std::cout << "{\"status\":\"error\",\"message\":\"Usage: /delete --sic <hex>\"}\n";
          } else {
            print_red("[ERROR] Usage: /delete --sic <hex>\n");
          }
          break;
        }
        try {
          std::string hex = sic_it->second;
          auto sic = parse_sic_hex(hex);
          bool found = engine.tombstone(sic);
          if (cmd.json) {
            print_json_delete(found, hex);
          } else if (found) {
            print_green("[OK] Deleted " + hex + "\n");
          } else {
            print_red("[ERROR] SIC not found: " + hex + "\n");
          }
        } catch (const std::exception& e) {
          if (cmd.json) {
            std::cout << "{\"status\":\"error\",\"message\":\"" << json_escape(e.what()) << "\"}\n";
          } else {
            print_red("[ERROR] " + std::string(e.what()) + "\n");
          }
        }
        break;
      }

      case silo::cli::Command::FETCH: {
        auto sic_it = cmd.args.find("sic");
        if (sic_it == cmd.args.end()) {
          if (cmd.json) {
            std::cout << "{\"status\":\"error\",\"message\":\"Usage: /fetch --sic <hex>\"}\n";
          } else {
            print_red("[ERROR] Usage: /fetch --sic <hex>\n");
          }
          break;
        }
        try {
          std::string hex = sic_it->second;
          auto sic = parse_sic_hex(hex);
          auto rec = engine.load(sic);
          if (cmd.json) {
            print_json_fetch(rec, hex);
          } else if (rec) {
            std::cout << "id=" << rec->id
                      << " dim=" << rec->dimension
                      << " ts=" << rec->timestamp << "\n";
            std::cout << "vec=[";
            for (size_t i = 0; i < rec->vector.size() && i < 5; ++i) {
              if (i > 0) std::cout << ",";
              std::cout << rec->vector[i];
            }
            if (rec->vector.size() > 5) std::cout << ",...";
            std::cout << "]\n";
          } else {
            print_red("[ERROR] SIC not found: " + hex + "\n");
          }
        } catch (const std::exception& e) {
          if (cmd.json) {
            std::cout << "{\"status\":\"error\",\"message\":\"" << json_escape(e.what()) << "\"}\n";
          } else {
            print_red("[ERROR] " + std::string(e.what()) + "\n");
          }
        }
        break;
      }

      case silo::cli::Command::COMPACT: {
        size_t freed = engine.compact();
        if (cmd.json) {
          print_json_compact(freed);
        } else {
          print_green("[OK] Compacted: " + std::to_string(freed) + " tombstones purged\n");
        }
        break;
      }

      case silo::cli::Command::STATUS: {
        auto s = engine.status();
        if (cmd.json) {
          print_json_status(s);
        } else {
          std::cout << "live_vectors=" << s.live_count
                    << " tombstones=" << s.tombstone_count
                    << " pages=" << s.page_count
                    << " disk=" << s.disk_usage << " bytes\n";
        }
        break;
      }

      case silo::cli::Command::HELP: {
        print_help();
        break;
      }

      case silo::cli::Command::EXIT: {
        std::cout << "Bye.\n";
        return 0;
      }

      default:
        std::cout << line << std::endl;
        break;
    }
  }

  return 0;
}
