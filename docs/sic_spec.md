# SIC (Structured Identifier) Specification

## Overview

SIC (Structured Identifier) is a 32-byte SHA256 hash that uniquely identifies a vector record. It is generated from the record's canonical serialisation and provides cryptographic verification of data integrity.

## Generation

The SIC is computed as:

```
input = id + '|' + to_string(dimension) + '|' + vector_bytes + '|' + to_string(timestamp)
SIC = SHA256(input)
```

Where:
- `id`: The record's identifier string (UTF-8, variable length)
- `|`: ASCII pipe character (0x7C) as field separator
- `dimension`: Decimal string representation of vector dimension (e.g., "8", "128")
- `vector_bytes`: Raw float32 bytes of the vector (little-endian IEEE 754)
- `timestamp`: Decimal string representation of Unix timestamp

## Verification

To verify a record's SIC:
1. Recompute the SIC from the record's fields using the same serialisation
2. Compare with the stored SIC
3. If they match, the record is unmodified

## Example

For a record with:
- id = "example"
- dimension = 4
- vector = [1.0, 2.0, 3.0, 4.0]
- timestamp = 1234567890

The serialised input is:
```
example|4|\x00\x00\x80?\x00\x00\x00@\x00\x00@@\x00\x00\x80@|1234567890
```

The SIC is SHA256 of these bytes.

## Properties

- Deterministic: Same fields always produce the same SIC
- Collision-resistant: SHA256 provides 2^128 collision resistance
- Tamper-evident: Any field modification changes the SIC
- Immutable binding: SIC binds id, dimension, vector, and timestamp together
