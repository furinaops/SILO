# On-Disk Page Format

## Page Size

Each page is exactly 8,192 bytes (8 KB).

## Header (12 bytes)

```
Offset  Size  Field          Description
------  ----  -----          -----------
0       4     page_id        Unique page identifier (uint32_t, little-endian)
4       4     checksum       First 4 bytes of SHA256 hash of bytes 12..8191
                               (i.e., all bytes after the checksum field).
                               Computed with checksum field zeroed.
8       2     record_count   Number of records stored in this page (uint16_t)
10      2     flags          Bit flags (uint16_t)
                               - Bit 0: reserved
                               - Bits 1-15: unused
```

Total header: 12 bytes. Payload: 8,192 - 12 = 8,180 bytes.

## Record Layout (within payload)

Records are written sequentially from offset 0 of the payload. Each record is variable-length.

```
Offset  Size  Field          Description
------  ----  -----          -----------
0       2     length         Total bytes of this record (uint16_t, little-endian).
                               Includes all fields including length itself.
2       2     flags          Bit flags (uint16_t)
                               - Bit 0: tombstone (1 = deleted)
                               - Bits 1-15: unused
4       8     timestamp      Unix epoch seconds (uint64_t, little-endian)
12      4     id_len         Length of ID string in bytes (uint32_t, little-endian)
16      4     dim            Vector dimension (uint32_t, little-endian)
20      32    sic            SHA256 SIC (32 bytes)
52      id_len  id           Record ID (UTF-8, no null terminator)
52+id   dim*4   vector       Float32 vector, dim * 4 bytes
_len
```

### Example: Record with id="abc", dim=2 vector=[1.0, 2.0]

```
Field       Bytes (hex)          Description
------      ------------          -----------
length      39 00                 57 bytes (little-endian)
flags       00 00                 not tombstoned
timestamp   2A 00 00 00 00 00    42 (little-endian)
            00 00
id_len      03 00 00 00          3 (little-endian)
dim         02 00 00 00          2 (little-endian)
sic         <32 bytes>           SHA256 hash
id          61 62 63             "abc"
vec[0]      00 00 80 3F          1.0f (IEEE 754 float32, little-endian)
vec[1]      00 00 00 40          2.0f (IEEE 754 float32, little-endian)
```

Total record size: 2 + 2 + 8 + 4 + 4 + 32 + 3 + 8 = 63 bytes.

## Maximum Records Per Page

Record size depends on id length and vector dimension:

- Id overhead: 52 bytes (fixed fields) + id_len + dim * 4
- Available payload: 8,180 bytes
- Max records per page ≈ 8180 / record_size

Examples:
- dim=8, id=8 chars: 52 + 8 + 32 = 92 bytes → ~88 records/page
- dim=128, id=8 chars: 52 + 8 + 512 = 572 bytes → ~14 records/page
- dim=384, id=8 chars: 52 + 8 + 1536 = 1596 bytes → ~5 records/page
- dim=4096, id=8 chars: 52 + 8 + 16384 = 16444 bytes → 0 records/page (exceeds page)

Maximum vector dimension is 4096 (fits within a single page: 52 + 8 + 16384 = 16444 > 8180 → dimension limited by page size).

## Checksum Verification

The checksum in the page header is the first 4 bytes of SHA256::hash(page_bytes[12..8191]). It is computed after every write and verified on page load. If the checksum does not match, the page is considered corrupt.

## Page Files

Pages are stored in files named `pages-<page_id>.bin` in the database directory. Each file contains exactly one page (8,192 bytes). The page_id in the header must match the file's numeric suffix.
