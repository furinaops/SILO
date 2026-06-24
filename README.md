# SILO — Structured Immutable Layered Object-store

> **Vector-native database with CASCADE approximate search.**  
> 148 KB binary. Zero dependencies. Runs on a 2011 i3 with 4 GB RAM.

```
$ python3 build.py
$ ./silo my_db
SILO v0.1.0
Database: my_db
Type /help for commands.
> /insert --id "my first vector" --vec [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8]
[OK] Inserted a1b2c3d4...
> /search --vec [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8] --top 3
   1. my first vector (score=1.0)
> /exit
Bye.
```

---

## What Problem Does It Solve?

Most databases organise data by **rows** or **documents** with exact-match lookups. Machine-learning embeddings — the float arrays produced by neural networks — don't fit that model. You need **similarity search**: "find the 10 most similar vectors to this one."

Traditional solutions are either:
- **Heavy**: Full-text search engines retrofitted with vector plugins, requiring Java, config files, and a server process.
- **Remote**: Cloud vector databases with per-query latency and data-residency concerns.
- **Complex**: Approximate Nearest Neighbour (ANN) libraries that trade accuracy for speed and require manual index tuning.

SILO takes the opposite approach: **a single binary, no server, no config, exact or approximate results**. It is designed for:
- Offline/edge vector search on a laptop or Raspberry Pi
- Embedding-indexing pipelines that need a local scratch database
- Privacy-sensitive workloads where data never leaves your machine

---

## How Is It Different From Traditional Databases?

| Feature | SQLite | PostgreSQL | SILO |
|---------|--------|-----------|------|
| **Data model** | Tables, rows, columns | Tables, rows, columns, JSONB | Vectors with SIC hashes |
| **Search type** | WHERE / JOIN (exact, B-tree) | WHERE / JOIN / GIN index | Cosine similarity (top-K) |
| **Lookup key** | Primary key (integer / TEXT) | Primary key / UUID / SERIAL | Cryptographic SIC (SHA256) |
| **Immutability** | UPDATE in-place | UPDATE in-place (MVCC) | Append-only + tombstone |
| **Concurrency** | Reader/writer locks, WAL mode | Multi-version concurrency control | Single-threaded CLI |
| **Vector search** | ❌ Not built-in (requires extension) | ❌ pgvector extension only | ✅ Native, SIMD-accelerated |
| **Approximate search** | ❌ | ❌ | ✅ CASCADE (tunable speed/recall) |
| **Dependencies** | libc, libm, libdl | ~40 MB of libs, config files | **Zero** — single 148 KB binary |
| **Startup** | `sqlite3 db.sqlite` (instant) | `pg_ctl start`, initdb, config | `./silo db` (instant) |
| **Server model** | Embedded (in-process) | Client-server (network) | Embedded (in-process) |
| **Use case** | General-purpose relational | Enterprise relational | Local vector / embedding search |

---

## Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│                          CLI (src/main.cpp)                          │
│  parse -> dispatch -> format  (/insert, /search, /delete, /status)   │
└───────────────────────────┬──────────────────────────────────────────┘
                            │
┌───────────────────────────▼──────────────────────────────────────────┐
│                     QueryEngine (src/query/)                          │
│  ┌───────────────────┐  ┌─────────────────────────────────────────┐  │
│  │  Brute-force      │  │  CASCADE (src/index/)                    │  │
│  │  SIMD dot product │  │  Forest of factor trees                  │  │
│  │  Min-heap top-K   │  │  Beam descent + pre-filter               │  │
│  └───────────────────┘  └─────────────────────────────────────────┘  │
└───────────────────────────┬──────────────────────────────────────────┘
                            │
┌───────────────────────────▼──────────────────────────────────────────┐
│                     StorageEngine (src/storage/)                      │
│  ┌──────────┐  ┌──────────┐  ┌────────────────┐  ┌───────────────┐  │
│  │  Pages   │  │  SIC     │  │  Tombstone     │  │  WAL          │  │
│  │  (8 KB)  │  │  Cache   │  │  Cache         │  │  (buffered)   │  │
│  └──────────┘  └──────────┘  └────────────────┘  └───────────────┘  │
└───────────────────────────┬──────────────────────────────────────────┘
                            │
┌───────────────────────────▼──────────────────────────────────────────┐
│                     Platform Layer (src/storage/platform.h)           │
│         mmap (POSIX) / CreateFileMapping (Windows)                   │
└──────────────────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────────────────────────────┐
│                     Crypto Layer (src/crypto/)                        │
│  SHA256 (from scratch) · SIC generate/verify · std::hash<SIC>        │
└──────────────────────────────────────────────────────────────────────┘
```

### Data Flow

```
INSERT:
  Record ─► Generate SIC (SHA256) ─► Append to WAL (buffered)
                                       ─► Write to Page (8 KB)
                                       ─► Update SIC cache

SEARCH (brute-force):
  Query vec ─► Precompute norm ─► load_all() live records
                ─► SIMD dot product (AVX2) ─► Min-heap top-K ─► Sort

SEARCH (cascade):
  Query vec ─► Pre-filter tree means ─► Beam descent in top-N trees
                ─► Global sort ─► Top-K

DELETE:
  SIC ─► Append DELETE entry to WAL ─► Mark tombstone in page in-place
         ─► Update tombstone cache

COMPACT:
  ─► Rewrite all pages, skip tombstones ─► Truncate WAL ─► Rebuild caches
```

---

## CASCADE — Approximate Nearest Neighbour Index

CASCADE (**C**oarse-to-fine **A**pproximate **S**earch with **D**eterministi**C** D**E**scent) is SILO's built-in approximate nearest neighbour index. It builds a forest of binary factor trees (128 → 64 → 32 → 16 centroids per tree) using deterministic K-means, then searches with configurable beam descent.

### The Algorithm: A Factor Tree for Vectors

Inspired by:
- **DeepSeek V4's KV cache** — hierarchical memory management
- **9th-grade math: the factor tree** — 128 → 64 → 32 → 16

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

### Performance Benchmarks

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

### Comparison to Other Tools

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

---

## Usage

### Build

```bash
python3 build.py           # release binary (./silo)
python3 build.py debug     # debug binary with ASan/UBSan (./silo-dbg)
python3 build.py test      # run all tests
python3 build.py bench     # run benchmarks
python3 build.py clean     # remove build artifacts
python3 build.py all       # clean → test → release
```

### Run

```bash
./silo                    # database in $XDG_DATA_HOME/silo (~/.local/share/silo)
./silo /path/to/db        # custom database directory
SILO_DB_DIR=/path ./silo  # via environment variable
```

### Commands

| Command | Example | Description |
|---------|---------|-------------|
| `/insert` | `/insert --id "photo" --vec [0.1,0.2,…]` | Insert a vector with an ID |
| `/search` | `/search --vec [0.1,0.2,…] --top 10` | Search top-K (brute-force) |
| `/search` | `/search --vec [0.1,0.2,…] --top 10 --algo cascade --trees 10` | Search top-K (CASCADE) |
| `/delete` | `/delete --sic <64-char-hex>` | Delete a vector by its SIC |
| `/fetch` | `/fetch --sic <64-char-hex>` | Retrieve a vector by its SIC |
| `/compact` | `/compact` | Purge all tombstoned records |
| `/build-cascade` | `/build-cascade` | Build CASCADE approximate index |
| `/status` | `/status` | Show database stats |
| `/help` | `/help` | Print command reference |
| `/exit` | `/exit` | Exit SILO |

### CASCADE Search Options

```
  --algo cascade       Use CASCADE approximate index
  --trees <N>          Number of trees to probe (default: 3)
  --trees auto         Auto-select via sqrt heuristic
  --trees all          Search all trees (exact recall)
  --probe <N>          Beam width (1=greedy, 3=multi-probe, default: 3)
```

### JSON Output

Append `--json` to any command for machine-readable output:

```bash
echo -e '/status --json\n/exit' | ./silo my_db
{"live_vectors":42,"tombstones":0,"pages":2,"disk_bytes":16384}
```

---

## Target Audience

- **Hackathon teams** who need a vector DB in 5 seconds
- **Students** without cloud budgets
- **Edge AI engineers** running on Raspberry Pi
- **Privacy advocates** who refuse to send data to the cloud
- **NGOs** with donated hardware from 2010
- **Offline developers** who can't rely on internet connectivity

---

## File Layout

```
build.py              # Single-command build system
src/
├── cli/parser.*      # Command-line parser
├── concurrency/      # RWLock with RAII guards
├── crypto/           # SHA256 + SIC implementation
├── index/            # CASCADE approximate index (cascade.h/cpp)
├── query/            # QueryEngine + SIMD dot product
├── storage/          # Page, WAL, StorageEngine, platform
├── util/             # Config, logging, timer
└── main.cpp          # CLI entry point
test/                 # 66+ unit tests
bench/                # Performance benchmarks
docs/                 # Architecture, page format, WAL format, SIC spec
```

---

## License

MIT — do what you want, just don't blame me.

## Credits

- **DeepSeek V4** for KV cache hierarchical partitioning inspiration
- **9th-grade math** for the factor tree (128 → 64 → 32 → 16)
- **Intel Core i3-2120** for forcing every microsecond optimization
- **VAULT** — SILO's SIC (Structured Identifier) was forked from my previous project, a personal cryptographic version-control system. The core idea of binding metadata to a SHA256 hash was inherited from VAULT's SIC design, but SILO reimplements the hashing from scratch (no code reuse) and tailors the serialisation format to vector-record semantics.
