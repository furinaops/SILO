# WAL (Write-Ahead Log) Binary Format

## File Naming

WAL segments are stored as `wal-<N>.log` where N is a zero-indexed sequence number. On startup, all existing WAL segments are read in order and replayed. After replay, the WAL is truncated.

## Entry Format

Each entry is a self-delimited binary record:

```
Offset  Size  Field        Description
------  ----  -----        -----------
0       4     entry_len    Total bytes of this entry (big-endian uint32_t).
                            Does NOT include these 4 bytes.
4       1     type         Entry type:
                            - 0 = INSERT
                            - 1 = DELETE
                            - 2 = COMPACT
5       8     timestamp    Unix epoch nanoseconds (big-endian uint64_t)
13      32    sic          SHA256-based Structured Identifier (32 bytes)
45      var   id           Record ID string (no null terminator, length
                            derived from remaining bytes after SIC for
                            DELETE; for INSERT, variable-length followed
                            by vector)
45+id   var   vector       Float32 vector bytes (INSERT only). Dimension
       _len                = vector_bytes / 4. The id length for INSERT
                            is the number of bytes before the vector data
                            (no separator between id and vector).
```

### INSERT Entry Example (hex dump)

Entry with id="abc" (3 bytes), dim=2 vector=[1.0, 2.0]:

```
00000000  2C 00 00 00  | entry_len = 44 (big-endian)
00000004  00           | type = INSERT
00000005  00 00 00 00  | timestamp = 42
         00 00 00 2A  |
0000000D  AA BB CC DD | SIC (32 bytes, example)
         ...          |
0000002D  61 62 63    | id = "abc"
00000030  00 00 80 3F | vector[0] = 1.0f (little-endian float32)
00000034  00 00 00 40 | vector[1] = 2.0f (little-endian float32)
```

Total on disk: 4 + entry_len = 48 bytes.

### DELETE Entry Example

Delete by SIC:

```
00000000  24 00 00 00  | entry_len = 36 (big-endian)
00000004  01           | type = DELETE
00000005  00 00 00 00  | timestamp = 99
         00 00 00 63  |
0000000D  SIC (32 bytes)
...
0000002D  (no id or vector for DELETE type)
```

## Crash Safety

Each entry is written via a buffered writer that flushes to disk when:
- The internal buffer reaches 64 KB
- 10 ms have elapsed since the last flush
- `flush()` is explicitly called (e.g., during `read_all()`, `truncate()`, or `close()`)

On crash recovery:
1. All WAL files in the DB directory are sorted by name
2. Each entry is read and replayed in order
3. After successful replay, the WAL is truncated
4. A new WAL segment is started
