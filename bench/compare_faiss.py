#!/usr/bin/env python3
"""Compare SILO search speed against FAISS (exact + IVF).

Usage:  python3 bench/compare_faiss.py <data.bin>
"""

import os
import struct
import sys
import time

import numpy as np
import faiss


def read_binary(path):
    with open(path, "rb") as f:
        n, dim = struct.unpack("<II", f.read(8))
        ids = []
        vectors = []
        for _ in range(n):
            (id_len,) = struct.unpack("<I", f.read(4))
            ids.append(f.read(id_len).decode("utf-8"))
            vec = list(struct.unpack(f"<{dim}f", f.read(dim * 4)))
            vectors.append(vec)
    return ids, vectors, dim


def benchmark_faiss(ids, vectors, dim):
    n = len(ids)
    arr = np.array(vectors, dtype=np.float32)
    query = np.array(vectors[0], dtype=np.float32).reshape(1, -1)

    print(f"  FAISS shape: {n} x {dim}")

    # ─── Exact search (IndexFlatIP) ───────────────────────────────
    print("\n  ── Exact search (IndexFlatIP, brute-force) ──")

    index_flat = faiss.IndexFlatIP(dim)
    faiss.normalize_L2(arr)
    index_flat.add(arr)

    query_norm = query.copy()
    faiss.normalize_L2(query_norm)

    # Warm-up
    index_flat.search(query_norm, 10)

    times = []
    for _ in range(100):
        t0 = time.perf_counter()
        index_flat.search(query_norm, 10)
        times.append((time.perf_counter() - t0) * 1000)

    times.sort()
    p50 = times[len(times) // 2]
    p95 = times[int(len(times) * 0.95)]
    p99 = times[int(len(times) * 0.99)]
    mean = sum(times) / len(times)
    print(f"    p50={p50:.3f} ms  p95={p95:.3f} ms  p99={p99:.3f} ms  mean={mean:.3f} ms")

    # ─── IVF (approximate) ────────────────────────────────────────
    if n >= 1000:
        nlist = max(1, int(n ** 0.5))
        print(f"\n  ── IVF{int(n ** 0.5)} (approximate, nprobe=10) ──")

        quantizer = faiss.IndexFlatIP(dim)
        index_ivf = faiss.IndexIVFFlat(quantizer, dim, nlist, faiss.METRIC_INNER_PRODUCT)
        index_ivf.train(arr)
        index_ivf.add(arr)
        index_ivf.nprobe = 10

        # Warm-up
        index_ivf.search(query_norm, 10)

        times = []
        for _ in range(100):
            t0 = time.perf_counter()
            index_ivf.search(query_norm, 10)
            times.append((time.perf_counter() - t0) * 1000)

        times.sort()
        p50 = times[len(times) // 2]
        p95 = times[int(len(times) * 0.95)]
        p99 = times[int(len(times) * 0.99)]
        mean = sum(times) / len(times)
        print(f"    p50={p50:.3f} ms  p95={p95:.3f} ms  p99={p99:.3f} ms  mean={mean:.3f} ms")

        # ─── IVF with more probes (accuracy vs speed) ─────────────
        index_ivf.nprobe = 50
        times = []
        for _ in range(100):
            t0 = time.perf_counter()
            index_ivf.search(query_norm, 10)
            times.append((time.perf_counter() - t0) * 1000)

        times.sort()
        p50 = times[len(times) // 2]
        p95 = times[int(len(times) * 0.95)]
        mean = sum(times) / len(times)
        print(f"  IVF nprobe=50      p50={p50:.3f} ms  p95={p95:.3f} ms  mean={mean:.3f} ms")

    # ─── Index size ───────────────────────────────────────────────
    idx_data = faiss.serialize_index(index_flat)
    print(f"\n  FAISS index size: {len(idx_data) / 1024:.0f} KB (serialised)")


def main():
    if len(sys.argv) < 2:
        print("Usage: compare_faiss.py <data.bin>")
        sys.exit(1)

    path = sys.argv[1]
    print(f"\n═══════════════════════════════════════════════════════")
    print(f" FAISS Comparison: {os.path.basename(path)}")
    print(f"═══════════════════════════════════════════════════════")
    ids, vectors, dim = read_binary(path)
    benchmark_faiss(ids, vectors, dim)


if __name__ == "__main__":
    main()
