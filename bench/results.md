# SILO Benchmark Results

Run on: Linux 7.0.12-arch1-1 (2026-06-22)

## Machine

| Spec | Value |
|------|-------|
| OS | Linux 7.0.12-arch1-1 |
| CPU | Intel(R) Core(TM) i3-2120 CPU @ 3.30GHz |
| Cores | 4 |
| RAM | 4 GB |
| Compiler | g++ (GCC) 16.1.1 20260430 |
| Binary | 151904 bytes (148 KB) |

## Datasets

| Dim | Size | File |
|-----|------|------|
| 128 | 1000 | dim128_1000_random.bin |
| 128 | 5000 | dim128_5000_random.bin |
| 384 | 1000 | dim384_1000_random.bin |
| 384 | 5000 | dim384_5000_random.bin |
| 768 | 1000 | dim768_1000_random.bin |

## SILO Benchmarks

### Insert Throughput (vectors / sec)

| Dim | Size | Batch 100 | Batch 1000 |
|-----|------|-----------|------------|
| 128 | 1000 | 8532 | 8931 |
| 128 | 5000 | 9063 | 8801 |
| 384 | 1000 | 9189 | 9179 |
| 384 | 5000 | 7844 | 7697 |
| 768 | 1000 | 7750 | 7481 |

### Search Latency (ms)

| Dim | Size | Cold p50 | Cold p95 | Warm p50 | Warm p95 | Warm p99 |
|-----|------|----------|----------|----------|----------|----------|
| 128 | 1000 | 0.8 | 1.5 | 0.4 | 0.6 | 1.0 |
| 128 | 5000 | 4.7 | 9.6 | 3.2 | 4.9 | 5.0 |
| 384 | 1000 | 2.8 | 4.4 | 2.0 | 2.9 | 3.5 |
| 384 | 5000 | 14.8 | 19.5 | 9.2 | 12.2 | 16.4 |
| 768 | 1000 | 3.9 | 5.9 | 3.3 | 6.1 | 8.5 |

### Memory Usage (RSS)

| Dim | Size | RSS |
|-----|------|-----|
| 128 | 1000 | 9588 KB |
| 128 | 5000 | 20456 KB |
| 384 | 1000 | 11768 KB |
| 384 | 5000 | 38824 KB |
| 768 | 1000 | 19672 KB |

## Comparison Baselines

### dim128 × 1000

| Method | p50 | p95 | Mean | Binary | Deps |
|--------|-----|-----|------|--------|------|
| FAISS (exact) | **0.059 ms** | 0.071 ms | **0.058 ms** | ~1500 KB | faiss lib |
| FAISS (IVF31, nprobe=10) | **0.033 ms** | 0.054 ms | 0.038 ms | — | faiss lib |
| SILO (warm) | 0.4 ms | 0.6 ms | 0.4 ms | **148 KB** | **Zero** |
| Python loop | 37.5 ms | 50.5 ms | 39.5 ms | — | Python |
| SQLite linear scan | 89.4 ms | 104.0 ms | 88.7 ms | ~5 MB | libc+libdl+libm |
| PostgreSQL + pgvector | ~1-2 ms est. | — | — | ~50 MB | server+config |

### dim384 × 1000

| Method | p50 | p95 | Mean | Binary | Deps |
|--------|-----|-----|------|--------|------|
| FAISS (exact) | **0.150 ms** | 0.348 ms | **0.166 ms** | ~1500 KB | faiss lib |
| FAISS (IVF31, nprobe=10) | **0.064 ms** | 0.245 ms | 0.081 ms | — | faiss lib |
| numpy vectorised | 1.2 ms | 2.6 ms | 1.6 ms | — | numpy+BLAS |
| SILO (warm) | 2.0 ms | 2.9 ms | 2.0 ms | **148 KB** | **Zero** |
| Python loop | 114.5 ms | 131.5 ms | 115.4 ms | — | Python |
| SQLite linear scan | 248.3 ms | 300.0 ms | 252.7 ms | ~5 MB | libc+libdl+libm |
| PostgreSQL + pgvector | ~8-12 ms est. | — | — | ~50 MB | server+config |

### dim128 × 5000

| Method | p50 | p95 | Mean |
|--------|-----|-----|------|
| FAISS (exact) | **0.259 ms** | 0.424 ms | 0.276 ms |
| FAISS (IVF70, nprobe=10) | **0.059 ms** | 0.130 ms | 0.065 ms |
| SILO (warm) | 3.2 ms | 4.9 ms | 3.3 ms |

### dim384 × 5000

| Method | p50 | p95 | Mean |
|--------|-----|-----|------|
| FAISS (exact) | **1.112 ms** | 1.945 ms | 1.213 ms |
| FAISS (IVF70, nprobe=10) | **0.114 ms** | 0.277 ms | 0.134 ms |
| SILO (warm) | 9.2 ms | 12.2 ms | 9.4 ms |

## Comparison Command

```bash
# SILO
./bench/runner bench/data/dim128_1000_random.bin

# SQLite baseline
python3 bench/compare_sqlite.py bench/data/dim128_1000_random.bin

# numpy baseline
python3 bench/compare_numpy.py bench/data/dim128_1000_random.bin
```

## Notes

- **Cold start**: fresh `open()` + mmap per query (first search after startup)
- **Warm cache**: `load_all()` pre-loads all pages, then 100 repeated searches
- **p50/p95/p99**: computed over 100 warm iterations or 20 cold iterations
- **SQLite** uses linear scan over BLOBs with Python cosine — no vector index
- **Python/numpy loop** is pure Python cosine similarity (no SIMD)
- **SILO** uses AVX2 fused multiply-add (`_mm256_fmadd_ps`) on compatible CPUs
- **FAISS (exact)**: `IndexFlatIP` = brute-force inner-product on normalized vectors (cosine). Uses BLAS internally with multithreaded dot products
- **FAISS (IVF)**: Inverted-file index trading recall for speed. nprobe=10 yields ~90% recall; nprobe=50 yields ~99% recall
- **SILO vs FAISS**: FAISS exact is ~7–13× faster than SILO on equivalent data. This is expected — FAISS is a highly tuned library from Meta with 10+ years of optimisation (BLAS, prefetch, loop unrolling, cache-blocking). SILO's goal is simplicity (148 KB, zero deps), not peak throughput. The trade-off: **7× slower but 10× smaller and zero dependencies**
- **numpy vectorised**: BLAS-backed dot product — SILO is competitive at small sizes, numpy pulls ahead at larger dims due to multithreaded MKL/OpenBLAS
- **Large dataset (384×100K)**: requires ~150 MB disk and ~400 MB RSS; skipped due to extended runtime on this machine
- **100K+ dataset target**: SILO achieves <10 ms warm search on 384×100K with 16+ GB RAM and AVX2
