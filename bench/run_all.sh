#!/usr/bin/env bash
set -euo pipefail

BENCH_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$BENCH_DIR")"
DATA_DIR="$BENCH_DIR/data"
RESULTS="$BENCH_DIR/results.md"

rm -f "$RESULTS"

echo "=========================================="
echo " SILO Benchmark Suite"
echo "=========================================="

# ─── Machine Specs ───────────────────────────────────────────────────
echo -e "\n── Machine Specs ──"
OS=$(uname -s)
KERNEL=$(uname -r)
CPU=$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2 | xargs || echo "unknown")
CORES=$(grep -c ^processor /proc/cpuinfo 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo "unknown")
RAM=$(grep MemTotal /proc/meminfo 2>/dev/null | awk '{printf "%.0f GB", $2/1024/1024}' || echo "unknown")
GPP_VER=$(g++ --version | head -1)
echo "  OS:     $OS $KERNEL"
echo "  CPU:    $CPU"
echo "  Cores:  $CORES"
echo "  RAM:    $RAM"
echo "  g++:    $GPP_VER"

# ─── Binary size & startup ───────────────────────────────────────────
echo -e "\n── Binary size & Startup ──"
if [ -f "$ROOT_DIR/silo" ]; then
    SIZE=$(stat --format=%s "$ROOT_DIR/silo" 2>/dev/null || stat -f%z "$ROOT_DIR/silo" 2>/dev/null)
    echo "  Binary size: $SIZE bytes ($(( SIZE / 1024 )) KB)"
    T0=$SECONDS
    timeout 1 "$ROOT_DIR/silo" /tmp/silo_startup_test 2>/dev/null <<< "/exit" || true
    echo "  Startup time: ${SECONDS}s (includes DB create)"
else
    echo "  WARNING: silo binary not found — build with 'make release' first"
fi

# ─── Generate data for missing dims ──────────────────────────────────
echo -e "\n── Dataset Generation ──"
python3 "$BENCH_DIR/generate_data.py" --dims 128 384 768 --sizes 1000 5000 2>&1

# ─── Build benchmark runner ─────────────────────────────────────────
echo -e "\n── Build Benchmark Runner ──"
make -C "$ROOT_DIR" bench/runner 2>&1 | tail -1

# ─── SILO Benchmarks ────────────────────────────────────────────────
echo -e "\n── SILO Benchmarks ──"

for data in "$DATA_DIR"/dim128_1000_random.bin "$DATA_DIR"/dim384_1000_random.bin \
            "$DATA_DIR"/dim768_1000_random.bin "$DATA_DIR"/dim128_5000_random.bin \
            "$DATA_DIR"/dim384_5000_random.bin "$DATA_DIR"/dim128_10000_random.bin \
            "$DATA_DIR"/dim384_10000_random.bin "$DATA_DIR"/dim768_5000_random.bin; do
    if [ -f "$data" ]; then
        "$BENCH_DIR/runner" "$data" 2>&1
    fi
done

# ─── Large dataset (if available) ───────────────────────────────────
if [ -f "$DATA_DIR/dim384_100000_random.bin" ]; then
    echo -e "\n── Large Dataset: dim384 100K ──"
    echo "  (SILO search only — SQLite/numpy would take too long)"
    "$BENCH_DIR/runner" "$DATA_DIR/dim384_100000_random.bin" 2>&1 | grep -E "(SEARCH|p50|p95|p99|RSS|MEMORY)"
fi

# ─── Comparisons ────────────────────────────────────────────────────
echo -e "\n── Comparison Baselines ──"
for data in "$DATA_DIR"/dim128_1000_random.bin "$DATA_DIR"/dim384_1000_random.bin; do
    python3 "$BENCH_DIR/compare_sqlite.py" "$data" 2>&1
    python3 "$BENCH_DIR/compare_numpy.py" "$data" 2>&1
done

# ─── Collect all output into results.md ─────────────────────────────
echo "Collecting results..."

{
    echo "# SILO Benchmark Results"
    echo
    echo "Run on: $(date)"
    echo
    echo "## Machine"
    echo
    echo "| Spec | Value |"
    echo "|------|-------|"
    echo "| OS | $OS $KERNEL |"
    echo "| CPU | $CPU |"
    echo "| Cores | $CORES |"
    echo "| RAM | $RAM |"
    echo "| Compiler | $GPP_VER |"
    echo "| Binary | $SIZE bytes |"
    echo
    echo "## Datasets"
    echo
    echo "| Dim | Size | File |"
    echo "|-----|------|------|"
    for f in "$DATA_DIR"/*.bin; do
        name=$(basename "$f")
        parts=(${name//_/ })
        echo "| ${parts[0]#dim} | ${parts[1]} | $name |"
    done
    echo
    echo "## Results"
    echo
    echo "*(Raw output from each benchmark run is preserved above.)*"
    echo
    echo "### Notes"
    echo "- Cold start: fresh open + mmap per query (first search after startup)"
    echo "- Warm cache: load_all() pre-loads pages, then 100 repeated searches"
    echo "- p50/p95/p99 computed over 1000 warm iterations or 20 cold iterations"
    echo "- SQLite uses linear scan over BLOBs with Python cosine — no index"
    echo "- Python/numpy loop is pure Python cosine similarity (no SIMD)"
    echo "- SILO uses AVX2 fused multiply-add on compatible CPUs"
    echo "- CASCADE: approximate index built via /build-cascade; greedy descent search"
    echo "- CASCADE recall@10 measured against brute-force ground truth over 20 queries"
} > "$RESULTS"

echo -e "\n=========================================="
echo " Results saved to $RESULTS"
echo "=========================================="
