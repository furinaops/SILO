#!/usr/bin/env python3
"""Compare SILO search speed against numpy brute-force cosine search.

Usage:  python3 bench/compare_numpy.py <data.bin>
"""

import os
import struct
import sys
import time

HAS_NUMPY = False
try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    pass


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


def cosine_similarity(a, b):
    dot = sum(x * y for x, y in zip(a, b))
    na = sum(x * x for x in a) ** 0.5
    nb = sum(x * x for x in b) ** 0.5
    return dot / (na * nb) if na * nb > 0 else 0


def benchmark_pure_python(vectors, dim):
    n = len(vectors)
    query = vectors[0]

    times = []
    for _ in range(20):
        t0 = time.perf_counter()
        scores = [(cosine_similarity(query, v), i) for i, v in enumerate(vectors)]
        scores.sort(key=lambda x: -x[0])
        top10 = scores[:10]
        times.append((time.perf_counter() - t0) * 1000)

    times.sort()
    p50 = times[len(times) // 2]
    p95 = times[int(len(times) * 0.95)]
    mean = sum(times) / len(times)
    print(f"  Python loop       p50={p50:.1f} ms  p95={p95:.1f} ms  mean={mean:.1f} ms")


def benchmark_numpy(vectors, dim):
    n = len(vectors)
    arr = np.array(vectors, dtype=np.float32)
    query = np.array(vectors[0], dtype=np.float32).reshape(1, -1)

    times = []
    for _ in range(20):
        t0 = time.perf_counter()
        norms = np.linalg.norm(arr, axis=1, keepdims=True)
        dot = arr @ query.T
        scores = (dot / (norms * np.linalg.norm(query))).flatten()
        top_idx = np.argsort(-scores)[:10]
        times.append((time.perf_counter() - t0) * 1000)

    times.sort()
    p50 = times[len(times) // 2]
    p95 = times[int(len(times) * 0.95)]
    mean = sum(times) / len(times)
    print(f"  numpy vectorised  p50={p50:.1f} ms  p95={p95:.1f} ms  mean={mean:.1f} ms")


def main():
    if len(sys.argv) < 2:
        print("Usage: compare_numpy.py <data.bin>")
        sys.exit(1)

    path = sys.argv[1]
    print(f"\n── numpy Comparison: {os.path.basename(path)} ──")
    ids, vectors, dim = read_binary(path)

    print(f"  shape: {len(ids)} x {dim}")

    if HAS_NUMPY:
        benchmark_numpy(vectors, dim)
    else:
        print("  numpy not available — using pure Python fallback")

    benchmark_pure_python(vectors, dim)


if __name__ == "__main__":
    main()
