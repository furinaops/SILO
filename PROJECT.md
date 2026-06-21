# SILO — Structured Immutable Layered Object-store

## Table of Contents

1. [Overview](#overview)
2. [Project Structure](#project-structure)
3. [Tech Stack](#tech-stack)
4. [Road Map](#road-map)
5. [Build & Run](#build--run)
6. [Development Guidelines](#development-guidelines)

---

## Overview

SILO is a single-node, vector-native database built from scratch in C++17. It stores high-dimensional float vectors (embeddings), searches them by similarity (cosine / L2), and supports parallel insert + delete operations — all without external dependencies, network overhead, or bloated query planners.

### Why SILO?

| Problem | SILO's Answer |
|---|---|
| Pinecone/Weaviate are distributed by default | Single-node. No Kubernetes, no gRPC, no cluster fees. |
| PostgreSQL stores vectors as `bytea` blobs | Vectors are first-class citizens. Distance functions live in the storage layer. |
| Most hobbyist DBs lock the whole dataset on write | Read-write locks — inserts/deletes run concurrently with each other and with reads. |
| "Just use FAISS" | FAISS is a library. SILO is a database — persistent, crash-recoverable, with a CLI and audit trail. |
| Vector DBs are black boxes | ~2,500 lines of C++17. Readable in an afternoon. |

### Core Features (MVP)

- **Native Vector Storage** — dense `float` vectors (dim 8–4096), SHA256 SIC (immutable, cryptographically verifiable), memory-mapped pages + append-only WAL.
- **Two-Way Parallel Ops** — `std::shared_mutex` guards the vector map; multiple `search()` calls run simultaneously, `insert()` and `delete()` run concurrently with each other, reads block only during the microsecond write-lock acquisition. Tombstone deletes (no physical removal); `/compact` purges tombstones.
- **Brute-Force Exact Search** — Cosine similarity + Euclidean distance, SIMD-accelerated (AVX2), returns top-K (SIC + score).
- **CLI Slash Commands** — `/insert`, `/search`, `/delete`, `/fetch`, `/compact`, `/status`, `/help`.
- **Zero External Dependencies** — C++17 Standard Library only + platform `mmap`. No Boost, no OpenSSL, no Postgres, no Redis.

---

## Project Structure

```
SILO/
├── Makefile                    # Top-level build (debug + release + clean)
├── README.md                   # Quick-start & overview
├── PROJECT.md                  # This file — structure, tech stack, road map
├── AGENTS.md                   # Guidelines for LLM coding agents
├── .clang-format               # C++ formatting rules
├── .clang-tidy                 # Static analysis config
├── .gitignore
│
├── src/
│   ├── main.cpp                # Entry point: CLI loop, /help, /status
│   ├── silo.h                  # Master header — forward-declares everything
│   │
│   ├── cli/
│   │   ├── parser.h            # Slash-command tokeniser & dispatcher
│   │   └── parser.cpp
│   │
│   ├── storage/
│   │   ├── engine.h            # StorageEngine: mmap pages, page table, load/store
│   │   ├── engine.cpp
│   │   ├── page.h              # Page struct (8 KB), page_id, checksum
│   │   ├── page.cpp
│   │   ├── wal.h               # Write-Ahead Log: append-only binary log, replay
│   │   ├── wal.cpp
│   │   ├── record.h            # Record: vector data, metadata, tombstone flag
│   │   └── record.cpp
│   │
│   ├── query/
│   │   ├── engine.h            # QueryEngine: brute-force search, cosine/L2
│   │   ├── engine.cpp
│   │   ├── simd.h              # AVX2 intrinsics wrappers (dot product, L2)
│   │   └── simd.cpp
│   │
│   ├── concurrency/
│   │   ├── rwlock.h            # RAII read/write lock guards (shared_mutex wrapper)
│   │   └── rwlock.cpp
│   │
│   ├── crypto/
│   │   ├── sha256.h            # SHA256 implementation (ported from VAULT)
│   │   ├── sha256.cpp
│   │   ├── sic.h               # SIC generation + verification helpers
│   │   └── sic.cpp
│   │
│   └── util/
│       ├── timer.h             # High-resolution stopwatch for latency logging
│       ├── timer.cpp
│       ├── logging.h           # Log level, coloured output, log file sink
│       ├── logging.cpp
│       ├── config.h            # TOML-less config: constants, env-var overrides
│       └── config.cpp
│
├── include/                    # (empty) — all headers live alongside .cpp files
│
├── test/
│   ├── Makefile                # Test build (links against src/ objects)
│   ├── test_main.cpp           # Test runner entry point
│   ├── test_vector.cpp         # Vector storage + retrieval
│   ├── test_search.cpp         # Cosine / L2 exactness + top-K ordering
│   ├── test_concurrency.cpp    # RW lock stress test (parallel insert/search)
│   ├── test_wal.cpp            # WAL append + replay + crash recovery
│   ├── test_sic.cpp            # SHA256 SIC generation + verification
│   ├── test_simd.cpp           # SIMD vs scalar fallback correctness
│   ├── test_tombstone.cpp      # Tombstone marking + /compact
│   └── test_cli.cpp            # Slash-command parser edge cases
│
├── bench/
│   ├── Makefile                # Benchmark build
│   ├── bench_main.cpp
│   ├── bench_search.cpp        # Throughput & latency for search (various N, dim)
│   ├── bench_insert.cpp        # Insert throughput (single-threaded + parallel)
│   └── bench_concurrency.cpp   # Read/write contention patterns
│
├── scripts/
│   ├── build.sh                # CI-friendly build wrapper
│   ├── lint.sh                 # Run clang-tidy + clang-format check
│   ├── test.sh                 # Build tests + run + print summary
│   └── bench.sh                # Build benchmarks + run + collect results
│
└── docs/
    ├── architecture.md         # Deep-dive into the design decisions
    ├── wal_format.md           # Binary layout of WAL entries
    ├── page_format.md          # On-disk page structure
    └── sic_spec.md             # How SIC is derived (field ordering, SHA256)
```

---

## Tech Stack

### Language & Standard

| Layer | Choice | Rationale |
|---|---|---|
| **Language** | C++17 (`-std=c++17`) | Stable standard with `std::shared_mutex`, `std::filesystem`, `std::optional`, `std::variant`. No C++20 requirement (toolchain portability). |
| **Build system** | GNU Make | Zero dependencies — `make` is everywhere. No CMake, no Meson. |
| **Compiler** | GCC 11+ / Clang 14+ | Both support AVX2 intrinsics and C++17 fully. |

### Libraries (Zero External)

| Component | Implementation |
|---|---|
| **SHA256** | Independently implemented after analysing VAULT's design — self-contained, ~300 lines, no OpenSSL. |
| **Memory-mapped I/O** | `mmap` (Linux), `CreateFileMapping` (Windows) — wrapped in `StorageEngine`. |
| **SIMD** | AVX2 intrinsics via `<immintrin.h>` — `_mm256_loadu_ps`, `_mm256_fmadd_ps`, etc. |
| **Concurrency** | `std::shared_mutex` + `std::jthread` (C++20) or `std::thread` (C++17 fallback). |
| **Filesystem** | `std::filesystem` (C++17) for directory setup, page file management. |
| **Hashing** | `std::hash` for in-memory indices; SHA256 for persistent SIC. |
| **CLI** | Custom parser — `std::string_view`, `std::span<char*>` argv-style tokenisation. |
| **Logging** | Custom — `enum class LogLevel { Debug, Info, Warn, Error }`, ANSI colour codes. |

### Platform Support

| Platform | Status | Notes |
|---|---|---|
| Linux (x86_64) | **Primary target** | `mmap`, AVX2, `sched_yield`. Tested on Ubuntu 22.04 / kernel 6.x. |
| macOS (ARM64) | **Secondary** | No AVX2 — NEON or scalar fallback. `mmap` works identically. |
| Windows (x64) | **Tertiary** | `CreateFileMapping`, `VirtualAlloc`. MSVC 2022 or Clang-cl. |

### Tooling (Developer Experience)

| Tool | Purpose |
|---|---|
| `clang-format` | Enforces consistent brace/style (Google or LLVM style). |
| `clang-tidy` | Catches `shared_mutex` misuse, uninitialised members, narrowing conversions. |
| `valgrind` / `ASan` | Memory-leak & use-after-free detection (CI gate). |
| `perf` / `flamegraph` | SIMD hot-path profiling. |
| `hyperfine` | CLI latency benchmarks (1 µs resolution). |

---

## Road Map

The road map is organised into **phases**. Each phase lists specific, actionable prompts that can be handed to an LLM coding agent or followed by a human developer.

### Phase 0 — VAULT Analysis & SIC Fork (Days 1–3)

**Goal:** Read and understand VAULT's source code, then independently fork its SIC logic into SILO's own crypto layer — no code copying, no OpenSSL dependency.

| # | Prompt / Task | Deliverable |
|---|---|---|
| 0.0a | **Analyse the VAULT source tree.** Read `VAULT/include/vault/sic.hpp` and `VAULT/src/sic.cpp`. Understand how `generate_sic` works: it concatenates fields (parent_sic, tree_hash, author_id, timestamp, name) separated by null bytes (`\0`), then SHA256-hashes the concatenation. Note that VAULT uses OpenSSL's `SHA256()` — SILO cannot use OpenSSL (zero deps requirement). Document the design in a short analysis note saved to `docs/vault_sic_analysis.md`. | `docs/vault_sic_analysis.md` exists, summarising the SIC algorithm and identifying what must be reimplemented. |
| 0.0b | **Analyse VAULT's SHA256 usage.** Confirm that VAULT relies entirely on `<openssl/sha.h>`. SILO must implement SHA256 from scratch (no OpenSSL). Read existing C++17 SHA256 reference implementations (e.g. the self-contained `sha256` header from the VAULT community or a public-domain implementation) to understand the algorithm (initialise → compress 64-byte blocks → finalise). Plan a standalone `src/crypto/sha256.h` that exposes `sha256(const uint8_t*, size_t) -> std::array<uint8_t, 32>` without any external includes. | A clear plan for the SHA256 port is noted in the same analysis doc. |
| 0.0c | **Design SILO's SIC format independently.** VAULT's SIC ties together version-tree metadata (parent_sic, tree_hash, author_id, timestamp, name). SILO's SIC must tie together vector identity: `id + '|' + dimension + '|' + vector_bytes + '|' + timestamp`. Decide on the canonical serialisation (byte layout, separator, endianness) and document it in `docs/sic_spec.md`. Write the spec before writing any code. | `docs/sic_spec.md` specifies the exact byte layout of a SIC input string, field ordering, and the hash output format. |

### Phase 0.5 — Project Skeleton (Week 1)

**Goal:** Buildable project with Makefile, empty source tree, formatting, and CI scripts.

| # | Prompt / Task | Deliverable |
|---|---|---|
| 0.1 | Scaffold the directory layout: create all directories and stub files listed in the project structure above. Every `.h` must include a header guard (or `#pragma once`) and a minimal comment; every `.cpp` must `#include` its corresponding header and define a single empty function `void silo_placeholder()` so the linker has a symbol. | `tree` shows the full structure; `make` links a `silo` binary that prints "not implemented" and exits 0. |
| 0.2 | Write the `Makefile` with targets: `release` (flattened `-O2 -march=native`), `debug` (`-O0 -g -fsanitize=address`), `clean`, `test`, `bench`, `lint`, `format`. Use `$(wildcard src/**/*.cpp)`. Avoid recursive make; single invocation. | `make release && make debug` both succeed without errors. |
| 0.3 | Create `.clang-format` (Google style, 120 column limit) and `.clang-tidy` (enable `bugprone-*`, `performance-*`, `modernize-*`). Write `scripts/lint.sh` that runs both tools on `src/` and fails on any warning. | `make lint` reports 0 issues on empty stubs. |
| 0.4 | Write `.gitignore` ignoring: `silo`, `*.o`, `*.a`, `test/runner`, `bench/runner`, `*.db`, `*.wal`, `*.log`, `build/`, `out/`. | `git status` is clean after `make`. |
| 0.5 | Implement a stub `main.cpp` that prints `SILO v0.1.0` and enters an interactive `readline`-style loop (using `std::getline`), echoing back whatever the user types. Accept `exit` and `quit` to break the loop. | `./silo` runs, accepts input, prints it back, exits on `exit`. |
| 0.6 | Write `scripts/build.sh` and `scripts/test.sh`: the former runs `make release`, the latter runs `make debug && make test`. Both exit non-zero on failure. | CI can invoke these scripts. |

### Phase 1 — SHA256 & SIC System (Week 1–2)

**Goal:** A standalone, tested SHA256 implementation and the SIC generation/verification layer — independently built from the VAULT analysis done in Phase 0.

| # | Prompt / Task | Deliverable |
|---|---|---|
| 1.1 | **Implement SHA256 from scratch** in `src/crypto/sha256.h` and `src/crypto/sha256.cpp`. Use the understanding gained from Phase 0.0b — do NOT copy VAULT's code. The API must expose: `SHA256::hash(const uint8_t* data, size_t len) -> std::array<uint8_t, 32>` and a helper `SHA256::hex(const std::array<uint8_t, 32>&) -> std::string`. No OpenSSL, no external dependencies. | Unit test verifies known test vectors (empty string, "abc", "abcdbcde..."). |
| 1.2 | In `src/crypto/sic.h`, define a `SIC` type (alias for `std::array<uint8_t, 32>`) with `to_string()` returning a 64-char hex string. Implement `SIC::generate(const Record&) -> SIC` that hashes the canonical serialisation: `id + '|' + dimension + '|' + vector_bytes + '|' + timestamp`. | A record with known fields produces a deterministic SIC. |
| 1.3 | Implement `SIC::verify(const Record&, const SIC&) -> bool`. This recomputes the hash and compares. | `verify` returns `true` for an unmodified record, `false` after any field is mutated. |
| 1.4 | Write `test/test_sic.cpp` with at least 10 test cases: empty ID, max-dimension vector, binary safety (null bytes in vector), timestamp edge cases, tamper detection. | `make test` runs and passes all SIC tests. |

### Phase 2 — Storage Engine (Week 2–3)

**Goal:** Persistent vector storage with memory-mapped pages, page table, and crash-recoverable WAL.

| # | Prompt / Task | Deliverable |
|---|---|---|
| 2.1 | Define `Page` struct in `src/storage/page.h`: 8192 bytes, containing a 16-byte header (page_id, checksum, record_count, flags) and 8176 bytes of payload. Implement `Page::compute_checksum()` using SHA256 truncated to 4 bytes. | Page header can be read/written; checksum catches single-bit flips. |
| 2.2 | Implement `StorageEngine::create()` in `src/storage/engine.cpp`: create the data directory (`silo.db/`), allocate the first page file (`pages-0001.bin`), mmap it. The engine should maintain an in-memory page table mapping `page_id -> mmap address`. | After `create()`, `ls silo.db/` shows `pages-0001.bin` of size 8192. |
| 2.3 | Implement `StorageEngine::store(const Record&) -> PageSlot`: find a page with room, write the record, update page header, mark page dirty. If no page has room, allocate a new page (extend file, remap). | Storing 1000 vectors of dim 128 produces roughly ceil(1000 / floor(8176/(128*4))) pages. |
| 2.4 | Implement `StorageEngine::load(sic) -> std::optional<Record>`: iterate pages, scan records, match SIC. Use a `std::unordered_map<SIC, PageSlot>` cache for O(1) lookups after initial load. | Lookup 10 000 stored vectors in < 1 ms after warm-up. |
| 2.5 | Implement `StorageEngine::load_all() -> std::vector<Record>` (used by search and /compact). Must skip tombstones. | Returns only live records. |
| 2.6 | Implement WAL in `src/storage/wal.h`: append-only binary log with entry type (INSERT, DELETE, COMPACT), timestamp, SIC, vector bytes. Each entry is prefixed with a 4-byte length + 1-byte type + 32-byte SIC. | `wal.append(entry)` writes to `silo.db/wal-0001.log`. |
| 2.7 | Implement `WAL::replay(StorageEngine&)`: on startup, read all WAL entries in order and re-apply INSERT/DELETE operations to the engine. After replay, truncate the WAL and start a fresh segment. | Crash recovery is lossless: insert 500 vectors, kill the process, restart — all 500 are present. |
| 2.8 | Write `test/test_wal.cpp`: append 1000 entries, simulate crash (skip replay), then replay and verify all records match. Test corrupt-entry skipping (length field with bad checksum). | WAL tests pass under ASan. |

### Phase 3 — Concurrency Layer (Week 3)

**Goal:** Thread-safe read-write access with `std::shared_mutex` and RAII guards.

| # | Prompt / Task | Deliverable |
|---|---|---|
| 3.1 | Define `RWLock` in `src/concurrency/rwlock.h` as a thin wrapper around `std::shared_mutex`. Provide `ReadGuard` (acquires shared lock) and `WriteGuard` (acquires exclusive lock). Both must be movable but not copyable. | `RWLock` compiles and basic lock/unlock works. |
| 3.2 | Integrate `RWLock` into `StorageEngine`: wrap `store()`, `load()`, `load_all()`, `tombstone()`, `compact()` with appropriate guard types. Multiple `load()` calls must be able to proceed in parallel. | A stress test with 4 readers + 2 writers does not deadlock. |
| 3.3 | Add a `ConcurrencyTest` in `test/test_concurrency.cpp`: spawn 4 threads doing continuous search, 2 threads doing insert, 1 thread doing delete. Run for 5 seconds. Assert no crashes, no data loss, and correct search results (tombstoned records never appear). | `make test` — test passes under TSan (ThreadSanitizer). |
| 3.4 | Implement `StorageEngine::tombstone(sic)`: mark the record's tombstone flag, write a DELETE entry to WAL. The in-memory cache must be updated atomically under a write lock. | After tombstone, `load(sic)` returns `nullopt`. |
| 3.5 | Implement `StorageEngine::compact()` under an exclusive write lock: iterate all pages, copy live records into a fresh page file, atomically swap the page table, unmap old pages, write a COMPACT entry to WAL, truncate WAL. | `/compact` reduces disk usage to only live records. |

### Phase 4 — Query Engine (Week 3–4)

**Goal:** Brute-force similarity search with SIMD acceleration.

| # | Prompt / Task | Deliverable |
|---|---|---|
| 4.1 | Implement `QueryEngine::search(const std::vector<float>& query, int top_k) -> std::vector<Result>` in `src/query/engine.cpp`. Iterate all live records, compute Cosine or L2 distance, maintain a min-heap of size `top_k`. | For 10 000 vectors dim 128, search returns correct top-5. |
| 4.2 | Implement `cosine_similarity` and `euclidean_distance` in `src/query/simd.cpp` using AVX2 intrinsics. For each pair of vectors, use `_mm256_dp_ps` (dot product) for cosine and `_mm256_sub_ps` + `_mm256_mul_ps` + horizontal add for L2. | SIMD dot product matches scalar result to within 1e-5 relative error. |
| 4.3 | Provide a scalar fallback (`#ifndef __AVX2__`) so the code compiles on ARM64 or without AVX2. Use `__attribute__((target("avx2")))` or runtime dispatch with `__builtin_cpu_supports`. | The binary runs on any x86_64 CPU, falling back gracefully. |
| 4.4 | Write `test/test_search.cpp`: generate 100 random vectors, insert them, search with a known query, verify that the top result has the highest cosine (or lowest L2). Test tie-breaking (equal scores should return both). | Search tests are deterministic (fixed seed). |
| 4.5 | Write `test/test_simd.cpp`: for every dim in {8, 16, 32, 64, 128, 256, 512, 1024, 4096}, compute dot product with both SIMD and scalar, assert near-equality. | Dim 4096 does not exceed 1e-4 relative error. |
| 4.6 | Write `bench/bench_search.cpp`: measure search latency for dataset sizes 100, 1 000, 10 000, 100 000 (dim 128) and dims 64, 256, 1024 (10 000 vectors). Report p50, p99, throughput (queries/sec). | Benchmarks print a table to stdout. |

### Phase 5 — CLI & Integration (Week 4–5)

**Goal:** All slash commands work end-to-end, producing the user experience described in the spec.

| # | Prompt / Task | Deliverable |
|---|---|---|
| 5.1 | Implement the slash-command parser in `src/cli/parser.cpp`. Tokenise the input line: command (e.g. `/insert`), then parse `--key value` pairs. Support quoted values for `--vec "[0.1, 0.2, ...]"`. Return a `ParsedCommand` struct with command enum + key-value map. | `/insert --id "foo" --vec "[1,2,3]"` parses correctly. |
| 5.2 | Wire commands in `main.cpp`: `/insert` → reads vector, generates SIC, calls `StorageEngine::store()`, prints SIC. `/search` → parses vector + top_k, calls `QueryEngine::search()`, prints ranked results. `/delete` → calls `tombstone()`, prints deletion SIC. `/fetch` → calls `load()`, prints vector + SIC + timestamp. `/compact` → calls `compact()`, prints freed count. `/status` → prints record count, tombstone count, disk usage. `/help` → prints all commands. `/exit` → clean shutdown. | End-to-end user flow from spec works. |
| 5.3 | Add colourised output in the CLI: green for success messages, yellow for warnings, red for errors. Use ANSI escape codes. Respect `NO_COLOR` environment variable. | Output is readable and visually scannable. |
| 5.4 | Implement input validation: reject vectors with dimension < 8 or > 4096, reject non-numeric `--top` values, reject empty `--id`. Print user-friendly error messages like `[ERROR] Dimension must be between 8 and 4096 (got: 2)`. | All invalid inputs produce helpful errors, not crashes. |
| 5.5 | Write `test/test_cli.cpp`: test parser with valid and invalid inputs, edge cases (extra spaces, missing values, `--vec` with malformed JSON, negative `--top`). | CLI parser coverage > 90%. |
| 5.6 | Implement clean shutdown: on `/exit` or SIGINT (Ctrl+C), flush WAL, sync all mmap'd pages (`msync`), unmap, close file descriptors. | No data loss on Ctrl+C. |
| 5.7 | Implement `StorageEngine::status()` returning: live vector count, tombstone count, total page count, disk usage (page files + WAL). Display via `/status`. | `/status` numbers match `du -sb silo.db/`. |

### Phase 6 — Tombstones & Compaction (Week 5)

**Goal:** Tombstone deletion works correctly and `/compact` restores space.

| # | Prompt / Task | Deliverable |
|---|---|---|
| 6.1 | Enhance `StorageEngine::tombstone()` to also update a tombstone counter. Ensure tombstones are skipped in `load_all()` and `QueryEngine::search()`. | Searches never return tombstoned records. |
| 6.2 | Write `test/test_tombstone.cpp`: insert N records, delete M of them, verify search returns N-M results. Verify that `/fetch --sic` on a deleted SIC returns `nullopt`. | Tombstone tests pass. |
| 6.3 | Implement the compaction algorithm in `StorageEngine::compact()`: under an exclusive write lock, allocate a fresh page file, copy live records (in insertion order), atomically swap page table, unmap old pages, update WAL. Print number of tombstones purged. | After compaction, disk usage equals `live_records * vector_size + overhead`. |
| 6.4 | Test compaction under concurrent load: in one thread, continuously insert + delete; in another thread, call `/compact` every 500 ms for 10 seconds. Assert no crashes, no data loss, and correct search results. | Stress test passes. |
| 6.5 | Ensure WAL recovery works after a crash during compaction: if the process crashes mid-compact (after new pages written but before page-table swap), recovery should discard the incomplete state and fall back to the previous page file. | Crash-during-compaction is safe. |

### Phase 7 — Performance & Polish (Week 5–6)

**Goal:** Optimise hot paths, benchmark, and harden edge cases.

| # | Prompt / Task | Deliverable |
|---|---|---|
| 7.1 | Profile `QueryEngine::search()` with `perf` on a dataset of 100 000 vectors dim 384. Identify the top 3 bottlenecks. Optimise: loop unrolling, prefetching (`_mm_prefetch`), batch distance computation. | Achieve < 10 ms per query for 100 K vectors dim 384. |
| 7.2 | Optimise WAL writes: batch multiple INSERT entries into a single `write()` syscall (use an in-memory buffer that flushes every 64 KB or every 10 ms). | WAL throughput exceeds 100 000 inserts/sec. |
| 7.3 | Reduce page-table lock contention: replace `std::shared_mutex` with a striped locking scheme (one lock per N pages) so that `store()` calls targeting different pages don't block each other. | Parallel insert throughput scales with core count. |
| 7.4 | Add a `--json` flag to all CLI commands for machine-readable output. `/search --vec "..." --top 5 --json` prints a JSON array of results. | Piping output to `jq` works. |
| 7.5 | Write `docs/architecture.md` with a deep-dive: page layout diagram, lock hierarchy, WAL entry binary format diagram, crash-recovery state machine. | Architecture doc is complete and accurate. |
| 7.6 | Write `docs/wal_format.md` and `docs/page_format.md` with exact byte offsets, endianness, and example hex dumps. | A developer can write a WAL reader in any language from the spec. |
| 7.7 | Add Windows support: `#ifdef _WIN32` for `CreateFileMapping`, `VirtualAlloc`, `OVERLAPPED` file writes. Wrap in `src/storage/platform.h`. | SILO compiles on MSVC 2022 x64. |
| 7.8 | Fuzz-test the CLI parser: generate random byte strings and feed them to the parser. Assert no crashes, infinite loops, or buffer overflows. Use a simple fuzz harness (no libFuzzer dependency). | 1 million random inputs produce 0 crashes. |

### Phase 8 — Documentation & Packaging (Week 6)

**Goal:** README, man-page-style help, and release packaging.

| # | Prompt / Task | Deliverable |
|---|---|---|
| 8.1 | Write `README.md` with: project description, build instructions, quick-start example (insert → search → delete), command reference table, architecture diagram (ASCII), and link to docs/. | README is self-contained and compelling. |
| 8.2 | Implement `/help` with detailed command documentation: usage, arguments, examples, notes on concurrency and crash safety. | `/help` output is a mini man page. |
| 8.3 | Create a release packaging script `scripts/package.sh` that builds `make release`, strips the binary, tarballs `silo` + `docs/` + `README.md` → `silo-<version>-<arch>.tar.gz`. | Package is < 1 MB compressed. |
| 8.4 | Write `AGENTS.md` instructing LLM coding agents: code style (Google style, no `using namespace std`, `#pragma once`), testing conventions (every feature needs a test case), and the rule "never add comments unless asked". | Agents.md matches this PROJECT.md's conventions. |

### Stretch Goals (Post-MVP)

| # | Goal | Notes |
|---|---|---|
| S.1 | Approximate Nearest Neighbour (ANN) — HNSW index for sub-ms search at 100 M vectors. | Requires graph construction, multi-level memory management. |
| S.2 | Persistent index: save/load HNSW graph alongside page files. | Enables fast restart. |
| S.3 | gRPC + Protobuf API for remote access. | Adds dependency; keep optional. |
| S.4 | REST API via a small embedded HTTP server (e.g. `libmicrohttpd` or custom). | For integration with Python/JS. |
| S.5 | Python bindings (`pybind11`) so `import silo` works. | Opens up ML ecosystem. |
| S.6 | Disk-backed vector cache (LRU) for datasets larger than RAM. | Uses `madvise` with `MADV_SEQUENTIAL`. |
| S.7 | ACID transactions with two-phase locking. | Adds complexity; evaluate if needed. |
| S.8 | Multi-node replication (single-writer, fan-out readers). | Distributed, but still minimal. |

---

## Build & Run

### Prerequisites

- **Linux x86_64** with GCC 11+ or Clang 14+
- `make`, `git`
- (Optional) `clang-format`, `clang-tidy`, `valgrind`, `hyperfine`

### Quick Start

```bash
git clone https://github.com/furinaops/silo
cd silo
make release
./silo
```

### Make Targets

| Target | Description |
|---|---|
| `release` | Optimised build (`-O2 -march=native -DNDEBUG`) |
| `debug` | Debug build (`-O0 -g -fsanitize=address,undefined`) |
| `test` | Build and run all unit tests |
| `bench` | Build and run all benchmarks |
| `lint` | Run `clang-tidy` and `clang-format --dry-run` |
| `format` | Auto-format all source files |
| `clean` | Remove all build artifacts |

---

## Development Guidelines

### Code Style

- **Formatting:** Google style enforced by `.clang-format` (column limit 120).
- **Headers:** `#pragma once` — no traditional include guards.
- **Namespaces:** Use `silo::` for all project code. Nested where appropriate: `silo::storage::`, `silo::query::`, `silo::crypto::`.
- **No `using namespace std;`** — qualify with `std::` explicitly.
- **No `malloc`/`free`** — use `std::vector`, `std::array`, `std::unique_ptr`.
- **Comments:** None unless the code is genuinely non-obvious (tricky SIMD, lock ordering invariants). Do not comment obvious code.
- **Error handling:** Return `std::optional<T>` or `silo::Result<T, Error>` instead of throwing exceptions. Exceptions are only for fatal/constructor failures.

### Testing

- Every feature must have corresponding tests in `test/`.
- Tests use a custom lightweight framework (no Google Test dependency): `TEST_CASE("name") { ... }` macro that registers and runs.
- All tests must pass under AddressSanitizer and ThreadSanitizer.
- Use seeded randomness (`std::mt19937` with fixed seed) for deterministic tests.

### Commit Conventions

- `feat:` for new features
- `fix:` for bug fixes
- `perf:` for performance improvements
- `test:` for test additions
- `docs:` for documentation
- `chore:` for build/tooling changes

---

*This document is a living specification. As SILO evolves, update this file to reflect the current state of the project.*
