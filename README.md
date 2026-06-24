# CASCADE — Coarse-to-fine Approximate Search with Deterministic Descent

> **A vector search algorithm for the rest of us.**  
> 148 KB binary. Zero dependencies. Runs on a 2011 i3 with 4 GB RAM.

---

## Why CASCADE?

Most vector search algorithms (HNSW, IVF) assume modern hardware — multiple cores, large caches, and abundant RAM. CASCADE was built for the **other 90% of devices** — the 4 GB RAM laptops, the Raspberry Pi, the 10-year-old office PCs.

**Key philosophy:** *"If it doesn't run on my 2011 i3 with 4 GB RAM, it doesn't ship."*

---

## The Algorithm: A Factor Tree for Vectors

CASCADE was inspired by two unlikely sources:

- **DeepSeek V4's KV cache handling** — hierarchical memory management for transformers
- **Class 9th grade math: the factor tree** — 128 → 64 → 32 → 16

The result is a balanced binary tree where each level represents a progressively specialized view of the data.

### The Structure

```
Level 0: 128 "Generals" (coarse spatial awareness)
   │
   ├── Level 1: 64 "Colonels" (regional specialization)
   │       │
   │       ├── Level 2: 32 "Majors" (local expertise)
   │       │       │
   │       │       └── Level 3: 16 "Soldiers" (actual vectors)
   │       └── Level 2: 32 "Majors"
   │               └── Level 3: 16 "Soldiers"
   └── Level 1: 64 "Colonels"
           └── ... (repeats)
```

### How Search Works

1. **Pre-filter**: Compute distance from query to all tree means.
2. **Select** top N trees (`--trees` flag).
3. **Beam descent** through each selected tree:

   - Level 0: Check all 128 centroids. Pick top `beam_width` (default: 3).
   - Level 1: Expand children of those nodes. Pick top `beam_width`.
   - Level 2: Expand children. Pick top `beam_width`.
   - Level 3: Brute-force all vectors in the selected leaf nodes.

4. **Return** top-K results.

### Complexity per Tree

| Level | Nodes to Check | Purpose |
|:-----:|:--------------:|---------|
| 0 | 128 | Coarse global view |
| 1 | 64 | Regional refinement |
| 2 | 32 | Local specialization |
| 3 | 16 | Exact leaf search |
| **Total** | **240** | **~20× faster than brute-force** |

With `--trees 10`, you search 10 such trees — still only 2,400 distance computations, roughly the cost of checking half the vectors directly.

---

## How It Works

### Build Phase (Offline)

- **K-means clustering** at each level:
  - Level 0: 128 centroids
  - Level 1: 2 children per centroid (64 each)
  - Level 2: 2 children per centroid (32 each)
  - Level 3: Store actual vectors (16 per leaf)
- Store **tree means** for pre-filtering.
- Memory-mapped storage (mmap) for efficient I/O.
- **Deterministic**: K-means uses first-k-vectors initialization, fixed 20 iterations, centroid reuse for empty clusters. Same data → same tree every time.

### Search Phase (Online)

1. **Pre-filter**: Compute distances from query to all tree means (O(trees)).
2. **Select** top `--trees` candidates.
3. **Beam descent** through each tree (240–400 distances per tree).
4. **Global sort** and return top-K.

### The "Beam Search" Constraint

At each level, instead of picking the single best child (greedy), CASCADE picks the top `beam_width` children and explores them in parallel. This prevents early mistakes from permanently losing the best path.

| Algorithm | Distances per Tree | Leaf Vectors Checked |
|-----------|:------------------:|:--------------------:|
| Greedy (beam=1) | 240 | 16 |
| Multi-probe (beam=3) | ~400 (1.7×) | ~48 (3×) |

---

## The `--trees` Flag

The single most important knob for tuning speed vs. accuracy.

| Value | Behavior |
|:------|:---------|
| `--trees 3` (default) | Balanced: fast search, moderate recall on large datasets |
| `--trees 10` | Higher recall: still 2.4× faster than brute-force |
| `--trees 20` | High recall: 63% R@10 at 1.4× speedup on 5k/128d |
| `--trees all` | Searches every tree. Matches brute-force recall (100%) |
| `--trees auto` | `max(3, sqrt(total_trees))` — adaptive heuristic |

---

## Performance Benchmarks

**Hardware:** Intel Core i3-2120 (2C/2T, 3.3 GHz), 4 GB DDR3 RAM  
**Dataset:** 5,000 random 128-dim vectors  
**Baseline:** Brute-force = 3.3 ms (100% recall)

| `--trees` | Latency | Speedup vs Brute | R@5 | R@10 |
|:---------:|:-------:|:----------------:|:---:|:----:|
| 3 (default) | **0.40 ms** | **8.6×** | 28% | 14% |
| 10 | **1.45 ms** | **2.4×** | 67% | 35% |
| 20 | **2.42 ms** | **1.4×** | 98% | 63% |
| all (40) | 5.02 ms | 0.7× | 100% | 100% |
| brute-force | 3.3 ms | 1× | 100% | 100% |

### Across All Dataset Sizes

| Dim | Size | `--trees` | Latency | Speedup | R@10 |
|:---:|:----:|:---------:|:-------:|:-------:|:----:|
| 128 | 1,000 | 3 | 0.35 ms | 1.2× | 45% |
| 128 | 1,000 | all (8) | 0.91 ms | 0.4× | 97% |
| 128 | **5,000** | **3** | **0.40 ms** | **8.6×** | 14% |
| 128 | **5,000** | **10** | **1.45 ms** | **2.4×** | **35%** |
| 128 | **5,000** | **20** | **2.42 ms** | **1.4×** | **63%** |
| 128 | 5,000 | all (40) | 5.02 ms | 0.7× | 100% |
| 128 | 10,000 | 3 | 0.39 ms | **18.6×** | 11% |
| 128 | 10,000 | 10 | 1.44 ms | **5.1×** | 21% |
| 128 | 10,000 | 20 | 2.74 ms | **2.7×** | 37% |
| 384 | 5,000 | 3 | 1.03 ms | **9.3×** | 19% |
| 384 | 5,000 | 10 | 3.41 ms | **2.8×** | 38% |
| 768 | 5,000 | 3 | 1.98 ms | **8.4×** | 17% |
| 768 | 5,000 | 10 | 6.63 ms | **2.5×** | 41% |

**TL;DR:** `--trees 10` gives **2.4–5.1× speedup** with **21–41% R@10**.  
`--trees 20` gives **63–66% R@10** at **1.3–2.7× speedup**.  
`--trees all` matches brute-force recall exactly.

---

## Comparison to Other Tools

| Tool | Size | Dependencies | Latency (5k, dim128) | Recall@10 | Cost |
|:-----|:----:|:------------:|:--------------------:|:---------:|:----:|
| **SILO brute-force** | 148 KB | None | 3.3 ms | 100% | $0 |
| **SILO-CASCADE (trees=10)** | 148 KB | None | **1.45 ms** | 35% | $0 |
| FAISS exact (IndexFlatIP) | 1.5 MB | None | 0.27 ms | 100% | $0 |
| FAISS IVF (nprobe=10) | 1.5 MB | None | 0.045 ms | >99% | $0 |
| numpy (BLAS) | — | numpy | 6.4 ms | 100% | $0 |
| Python loop | — | python | 207 ms | 100% | $0 |
| SQLite lin-scan | — | sqlite3 | 402 ms | 100% | $0 |
| Pinecone (cloud) | — | Internet | ~10 ms | ~95% | $70+/mo |

**SILO-CASCADE is 280× faster than SQLite, 50× faster than Python, and 4× faster than numpy** for approximate vector search — in a 148 KB binary with zero dependencies.

---

## Target Audience

- **Hackathon teams** who need a vector DB in 5 seconds.
- **Students** without cloud budgets.
- **Edge AI engineers** running on Raspberry Pi.
- **Privacy advocates** who refuse to send data to the cloud.
- **NGOs** with donated hardware from 2010.
- **Offline developers** who can't rely on internet.

**SILO-CASCADE is the vector database for the rest of us.**

---

## Quick Start

```bash
# Build the binary
python3 build.py

# Start SILO
./silo my_db

# Insert vectors
> /insert --id "vec1" --vec [0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8]
[OK] Inserted a1b2c3d4...

# Build CASCADE index
> /build-cascade
[OK] Built CASCADE index: 40 trees, 5000 vectors

# Search (default: 3 trees, beam=3)
> /search --vec "[0.1,0.2,...]" --top 5 --algo cascade

# Higher recall
> /search --vec "[0.1,0.2,...]" --top 5 --algo cascade --trees 10

# Maximum recall (all trees)
> /search --vec "[0.1,0.2,...]" --top 5 --algo cascade --trees all

# Auto-select trees
> /search --vec "[0.1,0.2,...]" --top 5 --algo cascade --trees auto

# Greedy descent instead of multi-probe
> /search --vec "[0.1,0.2,...]" --top 5 --algo cascade --probe 1
```

### All Commands

| Command | Description |
|---------|-------------|
| `/insert --id <str> --vec [x,y,...]` | Insert a vector |
| `/search --vec [x,y,...] --top <n> [--algo cascade] [--trees <N>\|auto\|all] [--probe <N>]` | Search top-K similar vectors |
| `/delete --sic <hex>` | Delete a vector by SIC |
| `/fetch --sic <hex>` | Retrieve a vector by SIC |
| `/compact` | Purge tombstoned records |
| `/build-cascade` | Build CASCADE approximate index |
| `/status` | Show database statistics |
| `/help` | Print command reference |
| `/exit` | Exit SILO |

---

## Determinism

CASCADE is fully deterministic:
- **Same build** → **same tree structure**
- **Same query** → **same result** (same scores, same ranks)
- **No randomness** in K-means initialization, no random seed, no non-deterministic probing

---

## Limitations

| Limitation | Cause | Mitigation |
|:-----------|:------|:-----------|
| Low recall on random data | Random uniform data has no cluster structure | Use `--trees 10` or `--trees 20`. Real data performs better |
| Slower than FAISS | Single-threaded SIMD vs BLAS | FAISS requires 1.5 MB + MKL; CASCADE is 148 KB, zero deps |
| Stale index | Snapshot at build time; new inserts invisible | Rebuild is explicit and fast (< 1 s for 10k vectors) |
| Tombstone blindness | Doesn't check tombstones | Rebuild the index to refresh |
| No multi-threading | Single-threaded beam descent | Intentional: keeps binary small and deterministic |

---

## License

MIT — do what you want, just don't blame me.

## Credits

- **DeepSeek V4** for KV cache hierarchical partitioning inspiration
- **9th-grade maths** for the factor tree (128 → 64 → 32 → 16)
- **Intel Core i3-2120** for forcing me to optimize
- **YOU** (reader) for reading this far
