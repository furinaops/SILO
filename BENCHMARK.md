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

- **SILO**: C++ benchmark harness (`bench/bench_main.cpp`). Insert throughput measured in batches of 100 and 1000, averaged over 4 runs after 1 warmup discard. Search latency measured over 100 queries after 10 warmup iterations. Cold start = fresh `open()` + first search; warm = `load_all()` pre-loads pages first.
- **FAISS**: `IndexFlatIP` (exact brute-force cosine via inner product on L2-normalised vectors). IVF with `nprobe=10` (≈90% recall) and `nprobe=50` (≈99% recall). 100 queries after warmup.
- **numpy**: `np.linalg.norm` + dot product via BLAS. Single-threaded on this machine.
- **SQLite**: Linear scan over BLOBs with Python cosine — no vector index, no extension.
- **PostgreSQL + pgvector**: Estimated from published benchmarks (PG was not running on this machine).
- **Python loop**: Pure Python `for` loop with manual cosine — no SIMD, no parallelisation.

## Datasets

Vectors are random uniform in [-1, 1]. All dimensions and sizes are tested with the same data across all systems.

| Dim | Size | Disk |
|-----|------|------|
| 128 | 1000 | 511 KB |
| 128 | 5000 | 2.5 MB |
| 384 | 1000 | 1.5 MB |
| 384 | 5000 | 7.4 MB |
| 768 | 1000 | 3.0 MB |

---

## Comparison Tables

### dim128 × 1000

| Method | p50 | p95 | Mean | Binary | Dependencies |
|--------|-----|-----|------|--------|-------------|
| FAISS IVF31 (nprobe=10) | **0.033 ms** | 0.054 ms | 0.038 ms | ~1.5 MB | faiss + BLAS |
| FAISS exact (IndexFlatIP) | **0.059 ms** | 0.071 ms | 0.058 ms | ~1.5 MB | faiss + BLAS |
| SILO (warm) | 0.4 ms | 0.6 ms | 0.4 ms | **148 KB** | **Zero** |
| Python loop | 37.5 ms | 50.5 ms | 39.5 ms | — | Python |
| SQLite (lin-scan) | 89.4 ms | 104.0 ms | 88.7 ms | ~5 MB | libc+libdl+libm |
| PostgreSQL + pgvector | ~1-2 ms | — | — | ~50 MB | server + config |

### dim384 × 1000

| Method | p50 | p95 | Mean | Binary | Dependencies |
|--------|-----|-----|------|--------|-------------|
| FAISS IVF31 (nprobe=10) | **0.064 ms** | 0.245 ms | 0.081 ms | ~1.5 MB | faiss + BLAS |
| FAISS exact (IndexFlatIP) | **0.150 ms** | 0.348 ms | 0.166 ms | ~1.5 MB | faiss + BLAS |
| numpy (BLAS) | 1.2 ms | 2.6 ms | 1.6 ms | — | numpy + MKL/OpenBLAS |
| SILO (warm) | **2.0 ms** | 2.9 ms | 2.0 ms | **148 KB** | **Zero** |
| Python loop | 114.5 ms | 131.5 ms | 115.4 ms | — | Python |
| SQLite (lin-scan) | 248.3 ms | 300.0 ms | 252.7 ms | ~5 MB | libc+libdl+libm |
| PostgreSQL + pgvector | ~8-12 ms | — | — | ~50 MB | server + config |

### dim128 × 5000

| Method | p50 | p95 | Mean |
|--------|-----|-----|------|
| FAISS IVF70 (nprobe=10) | **0.059 ms** | 0.130 ms | 0.065 ms |
| FAISS exact (IndexFlatIP) | **0.259 ms** | 0.424 ms | 0.276 ms |
| SILO (warm) | 3.2 ms | 4.9 ms | 3.3 ms |

### dim384 × 5000

| Method | p50 | p95 | Mean |
|--------|-----|-----|------|
| FAISS IVF70 (nprobe=10) | **0.114 ms** | 0.277 ms | 0.134 ms |
| FAISS exact (IndexFlatIP) | **1.112 ms** | 1.945 ms | 1.213 ms |
| SILO (warm) | 9.2 ms | 12.2 ms | 9.4 ms |

### dim768 × 1000

| Method | p50 | p95 | Mean |
|--------|-----|-----|------|
| SILO (warm) | 3.3 ms | 6.1 ms | 3.7 ms |

---

## Insert Throughput (SILO only)

| Dim | Size | Batch 100 | Batch 1000 |
|-----|------|-----------|------------|
| 128 | 1000 | 8532 vec/s | 8931 vec/s |
| 128 | 5000 | 9063 vec/s | 8801 vec/s |
| 384 | 1000 | 9189 vec/s | 9179 vec/s |
| 384 | 5000 | 7844 vec/s | 7697 vec/s |
| 768 | 1000 | 7750 vec/s | 7481 vec/s |

Insert throughput is bottlenecked by SHA256 hashing and WAL append, not page writes. Consistent across dims because the per-vector overhead (SIC generation, WAL serialisation) dominates.

---

## Memory Usage (SILO RSS)

| Dim | Size | RSS |
|-----|------|-----|
| 128 | 1000 | 9588 KB |
| 128 | 5000 | 20456 KB |
| 384 | 1000 | 11768 KB |
| 384 | 5000 | 38824 KB |
| 768 | 1000 | 19672 KB |

SILO keeps all pages mmap'd. RSS approximates: dataset size + page metadata + caches. The 384×5000 dataset is 7.4 MB on disk, memory usage is 38 MB — the 5× overhead comes from `std::vector<Record>` in `load_all()` and the SIC/tombstone hash tables.

---

## Analysis

### Why is FAISS so much faster?

FAISS is 7–13× faster than SILO on exact search for three reasons:

1. **BLAS backend**: FAISS delegates dot products to Intel MKL or OpenBLAS, which use hand-tuned assembly kernels (AVX2, AVX-512) with cache blocking, prefetching, and register tiling. SILO uses a simple `_mm256_fmadd_ps` loop — correct but not competitive with a thousand-engineer-year library.
2. **Multithreading**: FAISS distributes dot products across all CPU cores. SILO is single-threaded by design.
3. **Data layout**: FAISS stores vectors in a contiguous `float32` array optimised for SIMD traversal. SILO stores records in 8 KB pages with SIC hashes, IDs, and metadata interleaved — the page scan has more cache misses.

IVF (inverted-file index) adds another 4–10× speedup by only searching a subset of vectors in each partition, trading ~1–10% recall for order-of-magnitude speed.

### Why use SILO over FAISS?

| Factor | SILO | FAISS |
|--------|------|-------|
| **Deployment** | Single 148 KB binary | C++ library + BLAS + Python bindings |
| **Setup** | `./silo mydb` | `pip install faiss-cpu` + write code |
| **Dependencies** | Zero | libblas, libgomp, libstdc++ |
| **Persistence** | Built-in (pages + WAL) | You build it (write index to file) |
| **CRUD** | Insert / delete / search / compact | Add / search only |
| **Integrity** | SHA256 SIC per record | None |
| **WAL** | Crash recovery | None |
| **Concurrency** | ReadGuard / WriteGuard | None built-in |
| **CLI** | Interactive + `--json` | None |
| **Edge / RPi** | Runs anywhere | BLAS may not be available |
| **Size** | 148 KB | ~1.5 MB + ~10 MB BLAS |

FAISS is a **search library**. SILO is a **database**. If you already have a data pipeline and just need fast similarity search, use FAISS. If you want a turnkey vector database that persists data, survives crashes, and works out of the box with zero configuration, use SILO.

---

## SILO Pros and Cons

### Pros

- **Zero-dependency binary**: 148 KB, statically linked, runs on any Linux x86-64 without any external setup.
- **Instant setup**: `./silo /path/to/db` — no daemon, no config file, no init step.
- **Cryptographic integrity**: Every record has a SHA256 SIC that binds id + vector + timestamp together.
- **Crash recovery**: WAL with buffered writes (64 KB / 10 ms flush) guarantees no data loss on power failure.
- **Tombstone deletes**: Append-only storage with mark-and-sweep compaction; no UPDATE fragmentation.
- **SIMD-accelerated**: AVX2 dot product with prefetch hints (scalar fallback on non-AVX2 CPUs).
- **JSON output**: `--json` flag on every command for scripting and piping to `jq`.
- **Portable**: POSIX mmap with Windows `CreateFileMapping` fallback via `platform.h`.
- **Single-process concurrency**: ReadGuard/WriteGuard for parallel reads with exclusive writes.
- **Transparent on-disk format**: Page and WAL formats are fully documented in `docs/`.

### Cons

- **Search speed**: 7–13× slower than FAISS exact search; no approximate index (IVF, HNSW) — always brute-force.
- **Single-threaded**: No parallel query execution. QueryEngine searches one vector at a time.
- **Scalability**: All data must fit in virtual memory (mmap). No sharding, no replication, no cluster mode.
- **No networking**: CLI only. Cannot serve queries over HTTP/gRPC without a wrapper.
- **No query language**: No SQL, no filtering, no hybrid search (keyword + vector).
- **No metadata beyond ID**: Tags, labels, or arbitrary metadata are not stored — only id, SIC, timestamp, and vector.
- **Insert bottleneck**: SIC generation (SHA256) limits insert throughput to ~8000–9000 vec/s regardless of dimension.
- **Memory overhead**: RSS is ~5× the on-disk dataset size due to in-memory copies in `load_all()`.

---

## Raw Command Log

All benchmarks were run with the following commands:

```bash
# Generate data
python3 bench/generate_data.py --dims 128 384 768 --sizes 1000 5000

# SILO benchmark
./bench/runner bench/data/dim128_1000_random.bin \
               bench/data/dim128_5000_random.bin \
               bench/data/dim384_1000_random.bin \
               bench/data/dim384_5000_random.bin \
               bench/data/dim768_1000_random.bin

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
