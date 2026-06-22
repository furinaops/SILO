#!/usr/bin/env python3
"""Compare SILO search speed against PostgreSQL linear scan.

Usage:  python3 bench/compare_postgresql.py <data.bin>

Requires: PostgreSQL running locally, `psycopg2` or `pg8000` installed.
          Test database 'silo_bench' must exist or be creatable.
"""

import os
import struct
import subprocess
import sys
import time

try:
    import psycopg2
    HAS_PSYCOPG2 = True
except ImportError:
    HAS_PSYCOPG2 = False

try:
    import pg8000
    HAS_PG8000 = True
except ImportError:
    HAS_PG8000 = False


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


def benchmark_postgresql(ids, vectors, dim):
    if not HAS_PSYCOPG2 and not HAS_PG8000:
        print("  PostgreSQL comparison: SKIP (neither psycopg2 nor pg8000 installed)")
        return

    try:
        if HAS_PSYCOPG2:
            conn = psycopg2.connect(
                dbname="silo_bench", user=os.environ.get("PGUSER", "postgres"),
                password=os.environ.get("PGPASSWORD", ""),
                host=os.environ.get("PGHOST", "localhost"),
            )
        else:
            conn = pg8000.connect(
                database="silo_bench",
                user=os.environ.get("PGUSER", "postgres"),
                password=os.environ.get("PGPASSWORD", ""),
                host=os.environ.get("PGHOST", "localhost"),
            )
    except Exception as e:
        print(f"  PostgreSQL comparison: SKIP (connection failed: {e})")
        return

    cur = conn.cursor()

    # Create table and insert data
    cur.execute("DROP TABLE IF EXISTS vecs")
    cur.execute("CREATE TABLE vecs (id TEXT, vec FLOAT8[], dim INT)")
    conn.commit()

    n = len(ids)
    # Insert in batches
    batch_size = 100
    for start in range(0, n, batch_size):
        end = min(start + batch_size, n)
        rows = []
        for i in range(start, end):
            vec_str = "{" + ",".join(f"{v}" for v in vectors[i]) + "}"
            rows.append((ids[i], vec_str, dim))
        cur.executemany("INSERT INTO vecs VALUES (%s, %s, %s)", rows)
    conn.commit()

    query = vectors[0]
    query_str = "{" + ",".join(f"{v}" for v in query) + "}"

    print(f"  PostgreSQL shape: {n} rows x {dim} dims")

    # Warm-up run
    cur.execute(f"""
        SELECT id, 1 - (vec <=> '{query_str}'::float8[]) AS score
        FROM vecs ORDER BY score DESC LIMIT 10
    """)
    cur.fetchall()
    conn.commit()

    times = []
    for _ in range(20):
        t0 = time.perf_counter()
        cur.execute(f"""
            SELECT id, 1 - (vec <=> '{query_str}'::float8[]) AS score
            FROM vecs ORDER BY score DESC LIMIT 10
        """)
        cur.fetchall()
        conn.commit()
        times.append((time.perf_counter() - t0) * 1000)

    cur.execute("DROP TABLE vecs")
    conn.commit()
    conn.close()

    times.sort()
    p50 = times[len(times) // 2]
    p95 = times[int(len(times) * 0.95)]
    mean = sum(times) / len(times)
    print(f"  PostgreSQL pgvector  p50={p50:.1f} ms  p95={p95:.1f} ms  mean={mean:.1f} ms")


def main():
    if len(sys.argv) < 2:
        print("Usage: compare_postgresql.py <data.bin>")
        sys.exit(1)

    path = sys.argv[1]
    print(f"\n── PostgreSQL Comparison: {os.path.basename(path)} ──")
    ids, vectors, dim = read_binary(path)
    benchmark_postgresql(ids, vectors, dim)


if __name__ == "__main__":
    main()
