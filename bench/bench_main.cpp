#include "../src/crypto/sic.h"
#include "../src/index/cascade.h"
#include "../src/query/engine.h"
#include "../src/storage/engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Dataset {
  std::vector<std::string> ids;
  std::vector<std::vector<float>> vectors;
  size_t dim;
  size_t count;
};

struct Stats {
  double min, max, mean, median, p50, p95, p99, stddev;
};

Dataset read_dataset(const std::string& path) {
  Dataset ds;
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    std::cerr << "  ERROR: cannot open " << path << "\n";
    return ds;
  }

  uint32_t n, dim;
  f.read(reinterpret_cast<char*>(&n), sizeof(n));
  f.read(reinterpret_cast<char*>(&dim), sizeof(dim));
  ds.count = n;
  ds.dim = dim;

  for (uint32_t i = 0; i < n; ++i) {
    uint32_t id_len;
    f.read(reinterpret_cast<char*>(&id_len), sizeof(id_len));
    std::string id(id_len, '\0');
    f.read(id.data(), id_len);
    ds.ids.push_back(std::move(id));

    std::vector<float> vec(dim);
    f.read(reinterpret_cast<char*>(vec.data()), dim * sizeof(float));
    ds.vectors.push_back(std::move(vec));
  }
  return ds;
}

Stats compute_stats(std::vector<double>& samples) {
  if (samples.empty()) return {};
  std::sort(samples.begin(), samples.end());
  size_t n = samples.size();
  Stats s;
  s.min = samples.front();
  s.max = samples.back();
  s.mean = std::accumulate(samples.begin(), samples.end(), 0.0) / n;
  s.p50 = samples[n / 2];
  s.p95 = samples[static_cast<size_t>(n * 0.95)];
  s.p99 = samples[static_cast<size_t>(n * 0.99)];
  s.median = s.p50;

  double variance = 0;
  for (double v : samples) variance += (v - s.mean) * (v - s.mean);
  s.stddev = std::sqrt(variance / n);
  return s;
}

size_t current_rss_kb() {
#ifdef __linux__
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line)) {
    if (line.compare(0, 6, "VmRSS:") == 0) {
      return std::stoul(line.substr(6));
    }
  }
#endif
  return 0;
}

double to_ms(Clock::duration d) {
  return std::chrono::duration<double, std::milli>(d).count();
}

// ─── Insert Throughput ────────────────────────────────────────────────

void bench_insert(const Dataset& ds, const std::string& db_dir) {
  std::cout << "\n──────────────────────────────────────────────────────\n";
  std::cout << "INSERT THROUGHPUT — dim=" << ds.dim << "  n=" << ds.count << "\n";
  std::cout << "──────────────────────────────────────────────────────\n";

  for (int batch_size : {100, 1000}) {
    std::vector<double> runs_ms;
    for (int run = 0; run < 5; ++run) {
      std::filesystem::remove_all(db_dir);
      silo::storage::StorageEngine engine;
      engine.create(db_dir);
      std::vector<double> batches_ms;

      size_t inserted = 0;
      while (inserted < ds.count) {
        size_t end = std::min(inserted + batch_size, ds.count);
        auto t0 = Clock::now();
        for (size_t i = inserted; i < end; ++i) {
          silo::storage::Record rec;
          rec.id = ds.ids[i];
          rec.timestamp = static_cast<uint64_t>(i);
          rec.vector = ds.vectors[i];
          rec.dimension = static_cast<uint32_t>(ds.dim);
          auto sic = silo::crypto::SICUtils::generate(rec);
          rec.sic = sic;
          engine.store(rec, sic);
        }
        auto dt = Clock::now() - t0;
        batches_ms.push_back(to_ms(dt));
        inserted = end;
      }

      double total_ms = std::accumulate(batches_ms.begin(), batches_ms.end(), 0.0);
      if (run > 0) runs_ms.push_back(total_ms);
    }

    double avg_ms = std::accumulate(runs_ms.begin(), runs_ms.end(), 0.0) / runs_ms.size();
    double thru = (ds.count / (avg_ms / 1000.0));
    std::cout << "  batch=" << batch_size
              << "  " << std::fixed << std::setprecision(1) << thru << " vec/s"
              << "  (" << avg_ms << " ms total)\n";
  }
}

// ─── Search Latency ───────────────────────────────────────────────────

void bench_search(const Dataset& ds, silo::storage::StorageEngine& engine,
                  bool /*warm*/, const std::string& label) {
  silo::query::QueryEngine qe(engine);
  std::vector<double> all_ms;

  // Warm up: if warm, load_all happens in caller; do a few practice searches
  for (int i = 0; i < 10; ++i) {
    (void)qe.search(ds.vectors[i % ds.count], 10);
  }

  for (int iter = 0; iter < 100; ++iter) {
    auto& query = ds.vectors[iter % ds.count];
    auto t0 = Clock::now();
    (void)qe.search(query, 10);
    all_ms.push_back(to_ms(Clock::now() - t0));
  }

  Stats s = compute_stats(all_ms);
  std::cout << "  " << label
            << "  p50=" << s.p50 << " ms"
            << "  p95=" << s.p95 << " ms"
            << "  p99=" << s.p99 << " ms"
            << "  mean=" << s.mean << " ms"
            << "  ±" << s.stddev << "\n";
}

void bench_search_suite(const Dataset& ds, const std::string& db_dir) {
  std::cout << "\n──────────────────────────────────────────────────────\n";
  std::cout << "SEARCH LATENCY — dim=" << ds.dim << "  n=" << ds.count << "\n";
  std::cout << "──────────────────────────────────────────────────────\n";

  // --- Cold start: fresh open per query ---
  std::vector<double> cold_ms;
  for (int iter = 0; iter < 20; ++iter) {
    std::filesystem::remove_all(db_dir);
    {
      silo::storage::StorageEngine engine;
      engine.create(db_dir);
      for (size_t i = 0; i < ds.count; ++i) {
        silo::storage::Record rec;
        rec.id = ds.ids[i];
        rec.timestamp = static_cast<uint64_t>(i);
        rec.vector = ds.vectors[i];
        rec.dimension = static_cast<uint32_t>(ds.dim);
        auto sic = silo::crypto::SICUtils::generate(rec);
        rec.sic = sic;
        engine.store(rec, sic);
      }
    }
    if (iter > 0) {
      // Re-open fresh (cold) and run one search
      silo::storage::StorageEngine eng;
      eng.open(db_dir);
      silo::query::QueryEngine q(eng);
      auto t0 = Clock::now();
      (void)q.search(ds.vectors[0], 10);
      cold_ms.push_back(to_ms(Clock::now() - t0));
    }
  }
  if (!cold_ms.empty()) {
    Stats s = compute_stats(cold_ms);
    std::cout << "  cold    p50=" << s.p50 << " ms  p95=" << s.p95 << " ms  mean=" << s.mean << "\n";
  }

  // --- Warm: keep engine open ---
  std::filesystem::remove_all(db_dir);
  {
    silo::storage::StorageEngine engine;
    engine.create(db_dir);
    for (size_t i = 0; i < ds.count; ++i) {
      silo::storage::Record rec;
      rec.id = ds.ids[i];
      rec.timestamp = static_cast<uint64_t>(i);
      rec.vector = ds.vectors[i];
      rec.dimension = static_cast<uint32_t>(ds.dim);
      auto sic = silo::crypto::SICUtils::generate(rec);
      rec.sic = sic;
      engine.store(rec, sic);
    }

    // Pre-load into memory
    (void)engine.load_all();

    size_t rss_before = current_rss_kb();
    (void)engine.load_all(); // ensure pages are mapped
    size_t rss_after = current_rss_kb();

    bench_search(ds, engine, true, "warm ");
    std::cout << "  RSS delta: " << (rss_after - rss_before) << " KB\n";
  }
}

// ─── CASCADE Index ────────────────────────────────────────────────────

void bench_cascade(const Dataset& ds, const std::string& db_dir) {
  std::cout << "\n──────────────────────────────────────────────────────\n";
  std::cout << "CASCADE INDEX — dim=" << ds.dim << "  n=" << ds.count << "\n";
  std::cout << "──────────────────────────────────────────────────────\n";

  std::filesystem::remove_all(db_dir);
  silo::storage::StorageEngine engine;
  engine.create(db_dir);

  for (size_t i = 0; i < ds.count; ++i) {
    silo::storage::Record rec;
    rec.id = ds.ids[i];
    rec.timestamp = static_cast<uint64_t>(i);
    rec.vector = ds.vectors[i];
    rec.dimension = static_cast<uint32_t>(ds.dim);
    auto sic = silo::crypto::SICUtils::generate(rec);
    rec.sic = sic;
    engine.store(rec, sic);
  }

  silo::query::QueryEngine qe(engine);

  // ─── Build ──────────────────────────────────────────────────────────
  auto t0 = Clock::now();
  qe.build_cascade();
  double build_ms = to_ms(Clock::now() - t0);

  int trees = qe.cascade_num_trees();
  size_t rss = current_rss_kb();
  std::cout << "  Build       " << build_ms << " ms\n";
  std::cout << "  Trees       " << trees << "\n";
  std::cout << "  RSS after   " << rss << " KB\n";

  if (!qe.cascade_is_built()) {
    std::cout << "  SKIP (not built)\n";
    return;
  }

  // ─── Warm-up ────────────────────────────────────────────────────────
  for (int i = 0; i < 10; ++i) {
    (void)qe.search_cascade(ds.vectors[i % ds.count], 10);
  }

  // ─── Warm cascade search ────────────────────────────────────────────
  std::vector<double> cascade_ms;
  for (int iter = 0; iter < 100; ++iter) {
    auto& query = ds.vectors[iter % ds.count];
    auto t0 = Clock::now();
    (void)qe.search_cascade(query, 10);
    cascade_ms.push_back(to_ms(Clock::now() - t0));
  }
  Stats cs = compute_stats(cascade_ms);

  // ─── Warm brute-force search ────────────────────────────────────────
  std::vector<double> bf_ms;
  for (int iter = 0; iter < 100; ++iter) {
    auto& query = ds.vectors[iter % ds.count];
    auto t0 = Clock::now();
    (void)qe.search(query, 10);
    bf_ms.push_back(to_ms(Clock::now() - t0));
  }
  Stats bs = compute_stats(bf_ms);

  double speedup = bs.p50 / cs.p50;

  std::cout << "  Cascade     p50=" << cs.p50 << " ms  p95=" << cs.p95
            << " ms  mean=" << cs.mean << " ms\n";
  std::cout << "  Brute-force p50=" << bs.p50 << " ms  p95=" << bs.p95
            << " ms  mean=" << bs.mean << " ms\n";
  std::cout << "  Speedup     " << std::fixed << std::setprecision(2)
            << speedup << "x  (p50 ratio)\n";

  // ─── Recall@1, @5, @10 ──────────────────────────────────────────────
  int r1 = 0, r5 = 0, r10 = 0, rtotal = 0;
  for (int iter = 0; iter < 50; ++iter) {
    auto& query = ds.vectors[iter % ds.count];
    auto gt = qe.search(query, 10);
    auto cand = qe.search_cascade(query, 10);

    std::unordered_set<std::string> gt_sics;
    for (auto& r : gt) gt_sics.insert(r.sic_hex);

    for (size_t j = 0; j < cand.size(); ++j) {
      if (gt_sics.count(cand[j].sic_hex)) {
        if (j == 0) ++r1;
        if (j < 5) ++r5;
        ++r10;
      }
    }
    rtotal++;
  }
  double recall1 = 100.0 * r1 / std::max(rtotal, 1);
  double recall5 = 100.0 * r5 / std::max(rtotal * 5, 1);
  double recall10 = 100.0 * r10 / std::max(rtotal * 10, 1);
  std::cout << "  Recall@1    " << std::fixed << std::setprecision(1)
            << recall1 << "%\n";
  std::cout << "  Recall@5    " << std::setprecision(1)
            << recall5 << "%\n";
  std::cout << "  Recall@10   " << std::setprecision(1)
            << recall10 << "%  (" << rtotal << " queries)\n";
}

// ─── Memory Usage ─────────────────────────────────────────────────────

void bench_memory(const Dataset& ds, const std::string& db_dir) {
  std::cout << "\n──────────────────────────────────────────────────────\n";
  std::cout << "MEMORY USAGE — dim=" << ds.dim << "  n=" << ds.count << "\n";
  std::cout << "──────────────────────────────────────────────────────\n";

  std::filesystem::remove_all(db_dir);
  {
    silo::storage::StorageEngine engine;
    engine.create(db_dir);

    for (size_t i = 0; i < ds.count; ++i) {
      silo::storage::Record rec;
      rec.id = ds.ids[i];
      rec.timestamp = static_cast<uint64_t>(i);
      rec.vector = ds.vectors[i];
      rec.dimension = static_cast<uint32_t>(ds.dim);
      auto sic = silo::crypto::SICUtils::generate(rec);
      rec.sic = sic;
      engine.store(rec, sic);
    }

    engine.status();
    size_t rss = current_rss_kb();
    (void)engine.load_all();
    size_t rss_loaded = current_rss_kb();

    std::cout << "  RSS after insert: " << rss << " KB\n";
    std::cout << "  RSS after load_all: " << rss_loaded << " KB\n";
  }
}

// ─── Runner ───────────────────────────────────────────────────────────

void run_suite(const std::string& data_path, const std::string& db_dir) {
  Dataset ds = read_dataset(data_path);
  if (ds.count == 0) {
    std::cerr << "  Skipping " << data_path << " (empty or invalid)\n";
    return;
  }

  std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
  std::cout << "║  DATASET: dim=" << ds.dim << "  n=" << ds.count
            << "  file=" << data_path << "\n";
  std::cout << "╚══════════════════════════════════════════════════════════╝\n";

  bench_insert(ds, db_dir);
  bench_search_suite(ds, db_dir);
  bench_cascade(ds, db_dir);
  bench_memory(ds, db_dir);
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <data.bin> [data.bin ...]\n";
    return 1;
  }

  std::string db_dir = "/tmp/silo_bench_db";

  for (int i = 1; i < argc; ++i) {
    run_suite(argv[i], db_dir + "_" + std::to_string(i));
  }

  std::filesystem::remove_all(db_dir + "_1");
  std::filesystem::remove_all(db_dir + "_2");
  std::cout << "\nDone.\n";
  return 0;
}
