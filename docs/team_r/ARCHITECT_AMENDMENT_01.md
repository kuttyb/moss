# Moss Phase 24 — Team R: Architectural Amendment 01

**Author:** Independent L6 Architect, Moss Team R  
**Date:** October 12, 2026  
**Status:** **APPROVED & ADOPTED**  
**Integration Baseline:** `phase24-integration` (`43757790ef3af52204c90bbd9dabcefc4470e965`)  
**Target Branch:** `phase24-data-utils`  

---

## 1. Overview and Purpose

This Architectural Amendment formally updates the foundational architectural contract in `docs/team_r/ARCHITECT_REVIEW.md` in response to the successful delivery and integration of Moss Compiler Workstreams R0a (Native 64-bit Bitwise & Shift Operators) and R0b (Same-Module Type Resolution), and incorporates critical reference oracle and terminology corrections.

---

## 2. Adoption of Native 64-bit Bitwise Operations

### 2.1 Deprecation of Arithmetic Bitwise Emulation Layer
The previously specified arithmetic emulation layer (Euclidean division/modulo shifts, 256-entry byte XOR lookup tables, and simulated word operations) is **formally retired and superseded**.

### 2.2 Native Operator Set and Semantics
Subagents for `moss.hash`, `moss.compress`, and `moss.regex` must use native Moss Int64 operators directly:
- **Bitwise AND:** `a & b`
- **Bitwise OR:** `a | b`
- **Bitwise XOR:** `a ^ b`
- **Bitwise NOT:** `~a`
- **Bitwise Left Shift:** `a << b` (total: returns 0 if $b < 0$ or $b \ge 64$; wrapping shift otherwise)
- **Bitwise Right Shift:** `a >> b` (arithmetic sign-preserving: returns -1/0 if $b < 0$ or $b \ge 64$; arithmetic shift otherwise)

### 2.3 32-bit Modular Masking Conventions
Since Moss integers are signed 64-bit two's complement integers (`Int`), 32-bit algorithms (CRC-32, FNV-1a, Adler-32, SHA-256) must enforce explicit modular masking to maintain unsigned 32-bit invariants:
- **32-bit Mask:** `MASK32 = 4294967295` (or `0xFFFFFFFF`)
- **Byte Mask:** `MASK8 = 255`
- **Unsigned 32-bit Addition:** `(a + b) & 4294967295`
- **32-bit Rotation Right:** `((x >> k) & ((1 << (32 - k)) - 1)) | ((x << (32 - k)) & 4294967295)` (or `((x >> k) & 4294967295) | ((x << (32 - k)) & 4294967295)` with $0 \le x < 2^{32}$)

### 2.4 Strict Retention of Anti-Recursion Constraint
The architectural prohibition on recursion remains in full force. All state machine walks, tokenizers, parsers, and worklists must be strictly iterative.

---

## 3. Correction of Golden Reference Vectors

The reference vector table in Section 6 and Section 5 of `ARCHITECT_REVIEW.md` is corrected as follows:

| Algorithm | Input | Golden Decimal Result | Golden Hex Representation |
| :--- | :--- | ---: | :--- |
| **Adler-32** (RFC 1950) | `"123456789"` | **152961502** | `0x091E01DE` |
| **CRC-32** (IEEE 802.3) | `"123456789"` | **3421780262** | `0xCBF43926` |
| **FNV-1a 32-bit** | `"foobar"` | **3214735720** | `0xBF9AEAE8` |
| **Adler-32** | `""` | **1** | `0x00000001` |
| **CRC-32** | `""` | **0** | `0x00000000` |
| **FNV-1a 32-bit** | `""` | **2166136261** | `0x811C9DC5` |
| **SHA-256** | `""` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | - |
| **SHA-256** | `"abc"` | `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad` | - |

*Note on Errata:* The prior report noted Adler-32 as `152707547` and FNV-1a as `333333333` due to premature sketch placeholders. The values above are independently verified against Python `zlib`, Rust `crc32fast`, and standard RFC/NIST test oracles.

---

## 4. LZ4 Format Terminology and Specification Boundaries

To ensure strict interoperability without false standard conformance claims:
1. **Raw LZ4 Block Format (`moss.compress` core):**
   - Implements standard LZ4 block parsing (sequences of token byte, literal length extensions, literal bytes, 16-bit little-endian match offset, match length extensions).
   - Functions: `compress_lz4_block(src: Vector[Int]) -> Vector[Int]`, `decompress_lz4_block(src: Vector[Int], max_size: Int) -> Vector[Int]`.
2. **Explicit Size-Prefixed Block Format (Documented Convention):**
   - Prefixes a raw LZ4 block with 4-byte little-endian uncompressed length.
   - Distinct from a standard LZ4 Frame! Must be explicitly named and documented as `compress_lz4_size_prefixed` / `decompress_lz4_size_prefixed` (or with explicit documentation specifying this convention).
   - Under no circumstances shall this 4-byte prefix be advertised as standard "LZ4 Frame format".
3. **Standard Framed LZ4 (`.lz4` file format):**
   - Requires full specification: 4-byte magic number (`0x184D2204`), Frame Descriptor byte flags, Content Size (optional), Block Checksum (optional), Content Checksum (xxHash-32), and EndMark (`0x00000000`).
   - If full frame format is not implemented in v0.1, it must NOT be claimed.
4. **Interoperability Requirement:**
   - Raw blocks must roundtrip with Python `lz4.block.compress(uncompressed, store_size=False)` and `lz4.block.decompress(compressed, uncompressed_size=...)`.

---

## 5. Execution Matrix for Implementation Subagents

| Subagent | Module | Key Mandates |
| :--- | :--- | :--- |
| **R1** | `moss.regex` (Parser & Compiler) | Iterative Thompson NFA construction, precedence parsing, non-backtracking |
| **R2** | `moss.regex` (Engine & Matcher) | Parallel NFA state set simulation, match spans, search/replace |
| **R3** | `moss.compress` (LZ4 & Deflate) | Raw block compression/decompression, match copy overlap, bomb bounds |
| **R4** | `moss.hash` (Checksums & FNV-1a) | Native bitwise operators, table generation with `^` and shifts, golden vectors |
| **R5** | `moss.hash` (SHA-256) | Native bitwise Ch/Maj/Sigma, 32-bit wrapping addition, CAVP vectors |
| **R6** | Independent Reference Testing | Automated differential oracles vs Python/Rust standard libraries |
| **R7** | Cross-Package Integration & Benches | End-to-end log parsing + compression + hashing, release benchmark suite |

**Signed,**  
*Independent L6 Architect, Moss Team R*  
*October 12, 2026*
