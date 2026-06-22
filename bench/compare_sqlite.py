#!/usr/bin/env python3
"""Compare SILO search speed against SQLite linear scan over BLOBs.

Usage:  python3 bench/compare_sqlite.py <data.bin>
"""

import csv
import os
import struct
import subprocess
import sys
import tempfile
import time


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


def benchmark_sqlite(ids, vectors, dim):
    """Insert vectors as BLOBs in SQLite, then measure linear scan search."""

    try:
        import sqlite3
    except ImportError:
        print("  SQLite comparison: SKIP (sqlite3 not available)")
        return

    db_path = os.path.join(tempfile.mkdtemp(), "compare.db")
    conn = sqlite3.connect(db_path)
    conn.execute("CREATE TABLE vecs (id TEXT, vec BLOB, dim INT)")
    conn.execute("PRAGMA synchronous = OFF")
    conn.execute("PRAGMA journal_mode = OFF")

    n = len(ids)
    # Insert in a transaction
    conn.execute("BEGIN")
    for i in range(n):
        blob = struct.pack(f"<{dim}f", *vectors[i])
        conn.execute("INSERT INTO vecs VALUES (?, ?, ?)",
                     (ids[i], blob, dim))
    conn.execute("COMMIT")

    # Pick a query vector
    query = vectors[0]
    query_bytes = struct.pack(f"<{dim}f", *query)

    print(f"  SQLite shape: {n} rows x {dim} dims")

    # Warm-up
    _run_sqlite_search(conn, dim, query_bytes, n)

    times = []
    for _ in range(20):
        t0 = time.perf_counter()
        _run_sqlite_search(conn, dim, query_bytes, n)
        times.append((time.perf_counter() - t0) * 1000)

    conn.close()
    os.unlink(db_path)
    os.rmdir(os.path.dirname(db_path))

    times.sort()
    p50 = times[len(times) // 2]
    p95 = times[int(len(times) * 0.95)]
    mean = sum(times) / len(times)
    print(f"  SQLite lin-scan  p50={p50:.1f} ms  p95={p95:.1f} ms  mean={mean:.1f} ms")


def _run_sqlite_search(conn, dim, query_bytes, n):
    cur = conn.execute("SELECT id, vec FROM vecs")
    best_score = -1.0
    best_id = None
    import math
    for row in cur:
        blob = row[1]
        # Compute cosine similarity
        vec = struct.unpack(f"<{dim}f", blob)
        dot = sum(a * b for a, b in zip(vec, query_bytes))
        n1 = math.sqrt(sum(v * v for v in vec))
        n2 = math.sqrt(sum(v * v for v in query_bytes))
        score = dot / (n1 * n2) if n1 * n2 > 0 else 0
        if score > best_score:
            best_score = score
            best_id = row[0]


def main():
    if len(sys.argv) < 2:
        print("Usage: compare_sqlite.py <data.bin>")
        sys.exit(1)

    path = sys.argv[1]
    print(f"\n── SQLite Comparison: {os.path.basename(path)} ──")
    ids, vectors, dim = read_binary(path)
    benchmark_sqlite(ids, vectors, dim)


if __name__ == "__main__":
    main()
