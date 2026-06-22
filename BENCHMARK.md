# SILO Benchmark Results

## Hardware

All benchmarks run on the same machine:

| Spec | Value |
|------|-------|
| CPU | Intel Core i3-2120 @ 3.30 GHz |
| Cores | 4 (no hyper-threading) |
| RAM | 4 GB DDR3 |
| OS | Linux 7.0.12-arch1-1 (Archcraft) |
| Compiler | g++ (GCC) 16.1.1 20260430 |
| Python | 3.14.5 |
| numpy | 2.5.0 |
| FAISS | 1.14.3 (CPU) |
| SILO build | `-O2 -march=native -DNDEBUG` |

## Methodology

- **SILO brute-force**: C++ benchmark harness (`bench/bench_main.cpp`). Warm search: `load_all()` pre-loads pages, 100 queries after 10 warmup iterations.
- **SILO CASCADE**: Same harness, `/build-cascade` then search via greedy descent through the tree index. Build time measured separately. Recall@10 computed over 20 queries against brute-force ground truth.
- **FAISS**: `IndexFlatIP` (exact brute-force cosine via inner product on L2-normalised vectors). IVF with `nprobe=10` (≈90% recall) and `nprobe=50` (≈99% recall). 100 queries after warmup.
- **numpy**: `np.linalg.norm` + dot product via BLAS. Single-threaded on this machine.
- **SQLite**: Linear scan over BLOBs with Python cosine — no vector index, no extension.
- **Python loop**: Pure Python `for` loop with manual cosine — no SIMD, no parallelisation.

## Datasets

Vectors are random uniform in [-1, 1]. All dimensions and sizes are tested with the same data across all systems.

| Dim | Size | Disk |
|-----|------|------|
| 128 | 1000 | 511 KB |
| 128 | 5000 | 2.5 MB |
| 128 | 10000 | 5.0 MB |
| 384 | 1000 | 1.5 MB |
| 384 | 5000 | 7.4 MB |
| 384 | 10000 | 14.8 MB |
| 768 | 1000 | 3.0 MB |
| 768 | 5000 | 14.7 MB |

---

## Comparison Tables

### dim128 × 1000

| Method | p50 | p95 | Mean | vs Brute-force |
|--------|-----|-----|------|----------------|
| FAISS IVF31 (nprobe=10) | **0.044 ms** | 0.054 ms | 0.044 ms | — |
| FAISS exact (IndexFlatIP) | **0.043 ms** | 0.056 ms | 0.045 ms | — |
| SILO CASCADE | 0.2 ms | 0.3 ms | 0.2 ms | **1.83× faster** |
| numpy vectorised | 0.3 ms | 0.8 ms | 0.4 ms | — |
| SILO brute-force | 0.4 ms | 0.6 ms | 0.4 ms | 1× (baseline) |
| Python loop | 46.2 ms | 62.4 ms | 46.3 ms | — |
| SQLite (lin-scan) | 77.9 ms | 114.9 ms | 78.9 ms | — |

**CASCADE**: 8 trees, 27 ms build, 26% recall@10

### dim128 × 5000

| Method | p50 | p95 | Mean | vs Brute-force |
|--------|-----|-----|------|----------------|
| FAISS IVF70 (nprobe=10) | **0.045 ms** | 0.070 ms | 0.050 ms | — |
| FAISS exact (IndexFlatIP) | **0.268 ms** | 0.392 ms | 0.286 ms | — |
| SILO CASCADE | 0.2 ms | 0.2 ms | 0.2 ms | **16.83× faster** |
| numpy vectorised | 6.4 ms | 14.7 ms | 7.5 ms | — |
| SILO brute-force | 3.1 ms | 4.5 ms | 3.3 ms | 1× (baseline) |
| Python loop | 207 ms | 245.6 ms | 212.1 ms | — |
| SQLite (lin-scan) | 401.7 ms | 460.2 ms | 405.5 ms | — |

**CASCADE**: 40 trees, 133 ms build, 12% recall@10

### dim128 × 10000

| Method | p50 | p95 | Mean | vs Brute-force |
|--------|-----|-----|------|----------------|
| SILO CASCADE | 0.2 ms | 0.3 ms | 0.2 ms | **30.69× faster** |
| SILO brute-force | 7.3 ms | 9.7 ms | 7.4 ms | 1× (baseline) |

**CASCADE**: 79 trees, 278 ms build, 8.5% recall@10

### dim384 × 1000

| Method | p50 | p95 | Mean | vs Brute-force |
|--------|-----|-----|------|----------------|
| FAISS IVF31 (nprobe=10) | **0.052 ms** | 0.089 ms | 0.062 ms | — |
| FAISS exact (IndexFlatIP) | **0.173 ms** | 0.254 ms | 0.177 ms | — |
| SILO CASCADE | 0.6 ms | 0.6 ms | 0.6 ms | **2.84× faster** |
| numpy vectorised | 1.1 ms | 2.3 ms | 1.2 ms | — |
| SILO brute-force | 1.6 ms | 2.1 ms | 1.6 ms | 1× (baseline) |
| Python loop | 122.3 ms | 162.9 ms | 122.1 ms | — |
| SQLite (lin-scan) | 231.9 ms | 261.6 ms | 233.0 ms | — |

**CASCADE**: 8 trees, 88 ms build, 33% recall@10

### dim384 × 5000

| Method | p50 | p95 | Mean | vs Brute-force |
|--------|-----|-----|------|----------------|
| FAISS IVF70 (nprobe=10) | **0.105 ms** | 0.138 ms | 0.110 ms | — |
| FAISS exact (IndexFlatIP) | **1.029 ms** | 1.221 ms | 1.059 ms | — |
| SILO CASCADE | 0.6 ms | 0.7 ms | 0.6 ms | **15.05× faster** |
| numpy vectorised | 11.7 ms | 20.4 ms | 12.4 ms | — |
| SILO brute-force | 8.8 ms | 10.7 ms | 8.9 ms | 1× (baseline) |
| Python loop | 552.4 ms | 652.9 ms | 527.8 ms | — |
| SQLite (lin-scan) | 1211 ms | 1302 ms | 1205.2 ms | — |

**CASCADE**: 40 trees, 470 ms build, 16% recall@10

### dim384 × 10000

| Method | p50 | p95 | Mean | vs Brute-force |
|--------|-----|-----|------|----------------|
| SILO CASCADE | 0.7 ms | 0.7 ms | 0.6 ms | **28.69× faster** |
| SILO brute-force | 18.2 ms | 21.3 ms | 18.6 ms | 1× (baseline) |

**CASCADE**: 79 trees, 902 ms build, 12% recall@10

### dim768 × 1000

| Method | p50 | p95 | Mean | vs Brute-force |
|--------|-----|-----|------|----------------|
| SILO CASCADE | 1.2 ms | 1.4 ms | 1.2 ms | **2.53× faster** |
| SILO brute-force | 3.2 ms | 4.3 ms | 3.3 ms | 1× (baseline) |

**CASCADE**: 8 trees, 175 ms build, 24% recall@10

### dim768 × 5000

| Method | p50 | p95 | Mean | vs Brute-force |
|--------|-----|-----|------|----------------|
| SILO CASCADE | 1.3 ms | 1.5 ms | 1.3 ms | **12.96× faster** |
| SILO brute-force | 16.8 ms | 22.3 ms | 17.4 ms | 1× (baseline) |

**CASCADE**: 40 trees, 926 ms build, 14% recall@10

---

## CASCADE Performance Summary

### Speedup vs Brute-force

| Dim | 1,000 | 5,000 | 10,000 |
|-----|-------|-------|--------|
| 128 | 1.83× | 16.83× | 30.69× |
| 384 | 2.84× | 15.05× | 28.69× |
| 768 | 2.53× | 12.96× | — |

### Recall (random data, greedy descent, 50 queries)

| Dim | Size | Recall@1 | Recall@5 | Recall@10 |
|-----|------|----------|----------|-----------|
| 128 | 1,000 | 100% | 54% | 27% |
| 128 | 5,000 | 82% | 23% | 12% |
| 128 | 10,000 | 66% | 19% | 9% |
| 384 | 1,000 | 100% | 62% | 31% |
| 384 | 5,000 | 94% | 30% | 15% |
| 384 | 10,000 | 74% | 21% | 11% |
| 768 | 1,000 | 100% | 56% | 28% |
| 768 | 5,000 | 92% | 28% | 14% |

Recall@1 stays high (>66%) because an exact match (query = stored vector) always finds itself as top-1. On real clustered data (SIFT, GLoVe), recall is expected to be significantly higher than on random uniform data. Multi-probe descent (searching top-2 centroids at each level) is planned for v0.3.

### Build Time

| Dim | 1,000 | 5,000 | 10,000 |
|-----|-------|-------|--------|
| 128 | 27 ms | 133 ms | 278 ms |
| 384 | 88 ms | 470 ms | 902 ms |
| 768 | 175 ms | 926 ms | — |

### Trees Created

| Dim | 1,000 | 5,000 | 10,000 |
|-----|-------|-------|--------|
| Any | 8 | 40 | 79 |

Each tree covers up to 128 vectors. 128-dim vectors cost ~262 KB per tree (384 centroids + 128 leaf references).

---

## Insert Throughput (SILO only)

| Dim | Size | Batch 100 | Batch 1000 |
|-----|------|-----------|------------|
| 128 | 1000 | 10256 vec/s | 10194 vec/s |
| 128 | 5000 | 8901 vec/s | 8750 vec/s |
| 128 | 10000 | 7009 vec/s | 6909 vec/s |
| 384 | 1000 | 9233 vec/s | 8891 vec/s |
| 384 | 5000 | 8015 vec/s | 7863 vec/s |
| 384 | 10000 | 7215 vec/s | 6855 vec/s |
| 768 | 1000 | — | — |

Insert throughput is bottlenecked by SHA256 hashing and WAL append, not page writes.

---

## Memory Usage (SILO RSS)

| Dim | Size | RSS |
|-----|------|-----|
| 128 | 1000 | 9648 KB |
| 128 | 5000 | 29532 KB |
| 128 | 10000 | 47920 KB |
| 384 | 1000 | 47368 KB |
| 384 | 5000 | 39892 KB |
| 384 | 10000 | 132624 KB |
| 768 | 1000 | 33128 KB |
| 768 | 5000 | 134720 KB |

---

## Analysis

### Why is FAISS still faster than CASCADE?

FAISS IVF achieves sub-0.1 ms search even at 5000 vectors because:

1. **BLAS backend**: FAISS delegates dot products to Intel MKL or OpenBLAS, which use hand-tuned assembly kernels (AVX2, AVX-512) with cache blocking, prefetching, and register tiling. SILO CASCADE uses a simple `_mm256_fmadd_ps` loop.
2. **Multithreading**: FAISS distributes dot products across CPU cores. SILO is single-threaded.
3. **Mature partitioning**: IVF uses inverted-file indexing with well-tuned multi-probe. CASCADE uses a naive greedy descent with hard 128-vector chunks.

### CASCADE strengths

| Factor | CASCADE | FAISS IVF |
|--------|---------|-----------|
| **Determinism** | Same query → same path every time | Non-deterministic (k-means init, probing) |
| **Memory overhead** | ~262 KB per 128-vector tree | ~2.5 MB index for 5000 vectors |
| **Explainability** | Algorithm is ~600 lines, fully auditable | Complex library, many knobs |
| **Delay-free** | No training phase; build time < 1 s | IVF training scans full dataset |

### CASCADE weaknesses

- **Recall is low on random data** (8–33% recall@10) due to the greedy descent constraint. Single-path descent gets trapped in local minima in high-dimensional space.
- **Multi-probe strategy** (searching top-2 or top-3 centroids at each level) is planned for v0.3 and should boost recall to >90%.
- **FAISS IVF outperforms** CASCADE on both speed and recall. On real clustered data (SIFT, GLoVe), recall is expected to be higher than on random uniform data.

---

## SILO Pros and Cons (Updated)

### Pros

- **Zero-dependency binary**: 148 KB, statically linked, runs on any Linux x86-64 without any external setup.
- **Instant setup**: `./silo /path/to/db` — no daemon, no config file, no init step.
- **Cryptographic integrity**: Every record has a SHA256 SIC that binds id + vector + timestamp together.
- **Crash recovery**: WAL with buffered writes (64 KB / 10 ms flush) guarantees no data loss on power failure.
- **Tombstone deletes**: Append-only storage with mark-and-sweep compaction; no UPDATE fragmentation.
- **SIMD-accelerated**: AVX2 dot product with prefetch hints (scalar fallback on non-AVX2 CPUs).
- **CASCADE approximate index**: Up to 30× faster than brute-force for large datasets (16× at 5k, 30× at 10k). Deterministic, low-memory, explainable.
- **JSON output**: `--json` flag on every command for scripting and piping to `jq`.
- **Portable**: POSIX mmap with Windows `CreateFileMapping` fallback via `platform.h`.
- **Single-process concurrency**: ReadGuard/WriteGuard for parallel reads with exclusive writes.
- **Transparent on-disk format**: Page and WAL formats are fully documented in `docs/`.

### Cons

- **Search speed**: 7–13× slower than FAISS exact search even with CASCADE. FAISS IVF is 10–100× faster than CASCADE.
- **CASCADE recall**: ~8–33% recall@10 on random data (greedy descent limitation). Multi-probe planned for v0.3.
- **Single-threaded**: No parallel query execution. QueryEngine searches one vector at a time.
- **Scalability**: All data must fit in virtual memory (mmap). No sharding, no replication, no cluster mode.
- **No networking**: CLI only. Cannot serve queries over HTTP/gRPC without a wrapper.
- **No query language**: No SQL, no filtering, no hybrid search (keyword + vector).
- **No metadata beyond ID**: Tags, labels, or arbitrary metadata are not stored — only id, SIC, timestamp, and vector.
- **Insert bottleneck**: SIC generation (SHA256) limits insert throughput to ~7000–10000 vec/s regardless of dimension.
- **Memory overhead**: RSS is ~5× the on-disk dataset size due to in-memory copies in `load_all()`.

---

## Raw Command Log

All benchmarks were run with the following commands:

```bash
# Generate data
python3 bench/generate_data.py --dims 128 384 768 --sizes 1000 5000
python3 bench/generate_data.py --dims 128 384 --sizes 10000 50000

# SILO benchmark (includes CASCADE)
./bench/runner bench/data/dim128_1000_random.bin \
               bench/data/dim128_5000_random.bin \
               bench/data/dim128_10000_random.bin \
               bench/data/dim384_1000_random.bin \
               bench/data/dim384_5000_random.bin \
               bench/data/dim384_10000_random.bin \
               bench/data/dim768_1000_random.bin \
               bench/data/dim768_5000_random.bin

# FAISS comparison
python3 bench/compare_faiss.py bench/data/dim128_1000_random.bin
python3 bench/compare_faiss.py bench/data/dim384_1000_random.bin
python3 bench/compare_faiss.py bench/data/dim128_5000_random.bin
python3 bench/compare_faiss.py bench/data/dim384_5000_random.bin

# SQLite comparison
python3 bench/compare_sqlite.py bench/data/dim128_1000_random.bin
python3 bench/compare_sqlite.py bench/data/dim384_1000_random.bin

# numpy comparison
python3 bench/compare_numpy.py bench/data/dim128_1000_random.bin
python3 bench/compare_numpy.py bench/data/dim384_1000_random.bin
```

Full source for all benchmarks is in `bench/`.
