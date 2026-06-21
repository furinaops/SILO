# SILO — Structured Immutable Layered Object-store

SILO is a **vector-native database** built for local, single-node similarity search. It stores high-dimensional float vectors, retrieves them by cryptographic hash (SIC), and searches them by cosine similarity — all from a 150 KB binary with **zero external dependencies**.

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

SILO takes the opposite approach: **a single binary, no server, no config, exact results**. It is designed for:
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
| **Dependencies** | libc, libm, libdl | ~40 MB of libs, config files | **Zero** — single 150 KB binary |
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
│  Cosine/L2 similarity · SIMD dot product (AVX2) · Min-heap top-K     │
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

SEARCH:
  Query vec ─► Precompute norm ─► load_all() live records
                ─► SIMD dot product (AVX2) ─► Min-heap top-K ─► Sort

DELETE:
  SIC ─► Append DELETE entry to WAL ─► Mark tombstone in page in-place
         ─► Update tombstone cache

COMPACT:
  ─► Rewrite all pages, skip tombstones ─► Truncate WAL ─► Rebuild caches
```

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
| `/search` | `/search --vec [0.1,0.2,…] --top 10` | Search top-K similar vectors |
| `/delete` | `/delete --sic <64-char-hex>` | Delete a vector by its SIC |
| `/fetch` | `/fetch --sic <64-char-hex>` | Retrieve a vector by its SIC |
| `/compact` | `/compact` | Purge all tombstoned records |
| `/status` | `/status` | Show database stats |
| `/help` | `/help` | Print command reference |
| `/exit` | `/exit` | Exit SILO |

Append `--json` to any command for machine-readable output:

```bash
echo -e '/status --json\n/exit' | ./silo my_db
{"live_vectors":42,"tombstones":0,"pages":2,"disk_bytes":16384}
```

---

## Committing

```bash
git add -A && git commit -m "message"
```

---

## File Layout

```
build.py              # Single-command build system
src/
├── cli/parser.*      # Command-line parser
├── concurrency/      # RWLock with RAII guards
├── crypto/           # SHA256 + SIC implementation
├── query/            # QueryEngine + SIMD dot product
├── storage/          # Page, WAL, StorageEngine, platform
├── util/             # Config, logging, timer
└── main.cpp          # CLI entry point
test/                 # 47 unit tests
docs/                 # Architecture, page format, WAL format, SIC spec
```

---

## Performance Targets

| Metric | Target | Status |
|--------|--------|--------|
| Search 100K vectors (dim 384) | < 10 ms | ✅ (SIMD + prefetch) |
| Insert throughput | > 10K/s | ✅ (buffered WAL) |
| Binary size | < 200 KB | ✅ (~150 KB) |
| Dependencies | Zero | ✅ |

---

## Note

SILO's SIC (Structured Identifier) was forked from my previous project, **VAULT** — a personal cryptographic version-control system. The core idea of binding metadata to a SHA256 hash was inherited from VAULT's SIC design, but SILO reimplements the hashing from scratch (no code reuse) and tailors the serialisation format to vector-record semantics.
