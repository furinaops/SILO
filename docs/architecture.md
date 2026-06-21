# SILO Architecture

## Overview

SILO (Structured Immutable Layered Object-store) is a single-node, vector-native database. It stores high-dimensional float vectors, searches them by similarity (cosine / L2), and supports parallel insert + delete operations.

## Page Layout

```
 Page (8 KB)
 ┌─────────────────────────────────────────────────────┐
 │ Header (12 bytes)                                    │
 │  ├─ page_id      (uint32_t, offset 0)                │
 │  ├─ checksum     (uint32_t, offset 4, SHA256[0..3])  │
 │  ├─ record_count (uint16_t, offset 8)                │
 │  └─ flags        (uint16_t, offset 10)               │
 ├─────────────────────────────────────────────────────┤
 │ Payload (8180 bytes)                                 │
 │  ┌──────────┬──────────┬──────────┬──────────┐      │
 │  │ Record 1 │ Record 2 │ Record 3 │   ...    │      │
 │  └──────────┴──────────┴──────────┴──────────┘      │
 │                                                      │
 │ Each record on disk:                                 │
 │  ┌──────────────────────────────────────────┐       │
 │  │ length   (uint16_t, 2 bytes)              │       │
 │  │ flags    (uint16_t, 2 bytes, bit0=deleted)│       │
 │  │ timestamp(uint64_t, 8 bytes)              │       │
 │  │ id_len   (uint32_t, 4 bytes)              │       │
 │  │ dim      (uint32_t, 4 bytes)              │       │
 │  │ SIC      (32 bytes)                       │       │
 │  │ id       (id_len bytes)                   │       │
 │  │ vector   (dim * 4 bytes, float32)         │       │
 │  └──────────────────────────────────────────┘       │
 └─────────────────────────────────────────────────────┘
```

Pages are stored in files named `pages-<N>.bin` in the database directory.

## Lock Hierarchy

```
 StorageEngine
 ┌────────────────────────────────────────┐
 │  pages_ (vector<Page*>)                │
 │  sic_cache_ (unordered_map<SIC,Slot>)  │
 │  tombstone_cache_ (unordered_set<SIC>) │
 │  wal_ (WAL)                            │
 │                                         │
 │  store()   → Write lock on sic_cache_  │
 │  load()    → Read lock on sic_cache_   │
 │  load_all()→ Read lock on sic_cache_   │
 │  tombstone()→Write lock on both caches │
 │  compact() → Exclusive write lock      │
 └────────────────────────────────────────┘
```

The concurrency layer uses `std::shared_mutex` with RAII guards:
- `ReadGuard`: Multiple readers proceed in parallel
- `WriteGuard`: Exclusive access, blocks readers and other writers

## WAL Entry Binary Format

```
 ┌────────────────────────────────────────────┐
 │ entry_len  (uint32_t, big-endian)           │
 │ type       (uint8_t)                        │
 │   - 0 = INSERT                             │
 │   - 1 = DELETE                             │
 │   - 2 = COMPACT                            │
 │ timestamp  (uint64_t, big-endian)           │
 │ SIC        (32 bytes)                       │
 │ [id bytes (variable)]                       │
 │ [vector bytes (variable, for INSERT only)]  │
 └────────────────────────────────────────────┘
```

WAL files are named `wal-<N>.log`. After replay on startup, the WAL is truncated.

## Crash-Recovery State Machine

```
                     ┌─────────┐
                     │  START  │
                     └────┬────┘
                          │
                          ▼
                  ┌───────────────┐
                  │ Open/Create   │
                  │ DB Directory  │
                  └───────┬───────┘
                          │
                          ▼
                  ┌───────────────┐
                  │ Open WAL      │
                  │ Read entries  │────► Empty WAL
                  └───────┬───────┘     → skip
                          │
                          ▼
                  ┌───────────────┐
                  │ Replay WAL    │
                  │ entries into  │
                  │ StorageEngine │
                  └───────┬───────┘
                          │
                          ▼
                  ┌───────────────┐
                  │ Truncate WAL  │
                  │ Build SIC     │
                  │ Cache         │
                  └───────┬───────┘
                          │
                          ▼
                  ┌───────────────┐
                  │  READY        │
                  │ (CLI loop)    │
                  └───────┬───────┘
                          │
              ┌───────────┼───────────┐
              │           │           │
              ▼           ▼           ▼
        ┌─────────┐ ┌─────────┐ ┌─────────┐
        │ INSERT  │ │ DELETE  │ │ SEARCH  │
        │ Append  │ │ Append  │ │ Read    │
        │ WAL +   │ │ WAL +   │ │ Only    │
        │ Page    │ │ Mark    │ │         │
        └─────────┘ └─────────┘ └─────────┘
              │
              ▼
        ┌─────────┐
        │ COMPACT │
        │ Rewrite │
        │ Pages   │
        │ + Trunc │
        │ WAL     │
        └─────────┘

Crash during INSERT/DELETE:
  - WAL entry is safely on disk
  - On restart, replay WAL to reconstruct state

Crash during COMPACT:
  - Old pages remain intact until swap
  - On restart, discard incomplete state
  - Fall back to previous page file set
```

## Search Algorithm

```
QueryEngine::search(query, top_k):
  1. load_all() returns all live records
  2. Precompute query norm (cosine) if needed
  3. For each record:
     - Compute cosine similarity or L2 distance
     - Maintain min-heap of top_k results
  4. Return results sorted by score descending

SIMD acceleration (AVX2):
  - _mm256_loadu_ps / _mm256_fmadd_ps for dot product
  - Processes 8 floats per instruction
  - Scalar fallback on ARM64 or non-AVX2 CPUs
```
