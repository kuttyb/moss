# Phase 24 Team A — narrow capability requests to Team I

**Submitter:** Team A (Advanced Data SDK)

**Target:** Team I (Phase 24 integration owner)

**Status:** SUBMITTED / OPEN — documentation only; no capability is integrated

**Frozen architecture:** `ff4896c7a3c9ab191fdfa2443642e77b21f2ae4c`

**Inspection baseline:** `5394e93ceedccf21e577556325e598ff4d8c4d9c`

**Language/tool contract:** `moss-0.1`, compiler `0.1.0`, `moss-agent-1`

## Purpose

Team A requests two independent, reusable source capabilities:

1. exact conversion between a Moss `Float` and its raw IEEE-754 binary64 bits;
2. strict owned conversion between a Moss `String` and UTF-8 bytes.

The first capability is required for RFC 8949 Float64 encoding and decoding.
The second is required for native `String` adapters over Team A's lossless
UTF-8-byte representation. Neither request changes Team A's ownership model,
container design, or package dependency direction.

This document records a request, not an implementation or acceptance result.
Team A does not modify the compiler/runtime and does not claim either capability
is available until Team I integrates and validates it.

## Baseline classification

The frozen architecture establishes `Vector[Int]` values in `0..255` as the
public byte convention. Integer endian encoding is already feasible with
checked division and remainder over the full Moss `Int` range, so Team A does
not request bitwise operators for `moss.binary`.

The exact baseline evidence is:

- `&`, `<<`, and `>>` are rejected with
  `UNSUPPORTED_EXPRESSION_OPERATOR`. They are not needed by the proposed API.
- Unary `~` is falsely accepted by the checker as unresolved `_value`; Fast
  Debug rejects it as an unsupported expression, and native lowering emits an
  invalid Rust operator. This is a checker defect, not a supported workaround.
- `1.0.to_bits()` is falsely accepted by the checker as unresolved `_value`.
  Native Debug and Release happen to leak the Rust method and produce
  `4607182418800017408` for `1.0`, while Fast Debug rejects the call. An
  exported wrapper declared to return `Int` fails with
  `TYPE_INFERENCE_FAILED`, so this spelling cannot cross ABI-v9.
- A reciprocal receiver spelling such as `seed.from_bits(bits)` is likewise
  falsely accepted by the checker, rejected by Fast Debug, and rejected by
  native Rust compilation because `from_bits` is an associated function rather
  than a receiver method.
- Bootstrap and the established Text/Encoding contract expose no supported
  `String`-to-UTF-8 or UTF-8-to-`String` conversion. Existing
  `moss.encoding` operations strictly convert UTF-8 bytes and Unicode scalar
  integers, but cannot cross the native `String` boundary.

The primitive-member and unary-operator results must be repaired or rejected
independently. Team A will not depend on accidental native backend leakage.

## Request 1: exact Float raw-bit pair

### Minimal source surface

```moss
export fn float_to_raw_bits(value: Float) -> Int
export fn float_from_raw_bits(bits: Int) -> Float
```

`Int` is the signed two's-complement carrier for all 64 raw bits. The functions
reinterpret bits; they do not perform numeric conversion. They are total and
non-raising for every Moss `Float` and every 64-bit `Int` pattern.

### Required semantics

- Preserve positive and negative zero, normal values, subnormals, positive and
  negative infinity, and every NaN sign/payload pattern.
- For every `Int` bit pattern `b`,
  `float_to_raw_bits(float_from_raw_bits(b)) == b`.
- For every non-NaN `Float` value `x`, conversion to bits and back preserves the
  exact value and sign. NaN round trips are judged by raw bits, not `Float`
  equality.
- No decimal formatting/parsing, integer numeric cast, lossy canonicalization,
  or host-endian byte reinterpretation may substitute for the raw conversion.
- The functions are pure value operations with concrete checked signatures.
  Their inferred access is `READ`, and they introduce no observable effect.

### Required implementation parity

- **Checker:** resolve both names to the exact signatures above and reject
  arbitrary primitive member calls instead of leaving unresolved `_value`
  expressions that leak into a backend.
- **Native Debug and Release:** reinterpret the complete binary64 pattern in
  both directions without changing any bit.
- **Fast Debug:** implement the same conversion directly. It must not reject
  the operation or silently fall back to native execution.
- **ABI-v9:** preserve the concrete signatures and effects in the semantic
  interface. A compiled-only consumer with no provider `.moss` source must
  build and run in Native Debug and Release with identical results. Fast Debug
  remains source-backed under its existing contract.

### Acceptance vectors

The acceptance suite must cover at least these raw patterns. Patterns with the
high bit set are carried as the corresponding signed two's-complement `Int`.

| Raw bits | Meaning |
| --- | --- |
| `0x0000000000000000` | positive zero |
| `0x8000000000000000` | negative zero / `INT64_MIN` carrier |
| `0x3ff0000000000000` | `1.0` |
| `0xbff0000000000000` | `-1.0` |
| `0x0000000000000001` | smallest positive subnormal |
| `0x7fefffffffffffff` | largest finite positive value |
| `0x7ff0000000000000` | positive infinity |
| `0xfff0000000000000` | negative infinity |
| `0x7ff8000000000001` | positive quiet NaN with payload |
| `0xfff8000000000042` | negative quiet NaN with payload |

For each pattern, test bits-to-Float-to-bits in Native Debug, Native Release,
Fast Debug, and a source-free ABI-v9 consumer. Compare the fixed patterns and
CBOR network-order bytes with an independent oracle such as Python `struct` or
a Rust `f64::{from_bits,to_bits}` harness. RFC 8949 vectors must include at
least Float64 `1.1` (`fb3ff199999999999a`), both signed zeros, infinities, a
subnormal, and NaN payload handling.

## Request 2: strict String/UTF-8 conversion pair

### Minimal source surface

```moss
export enum Utf8ConversionError:
  InvalidByte(index: Int, value: Int)
  InvalidUtf8(index: Int)

export fn string_utf8_bytes(value: String) -> Vector[Int]
export fn string_from_utf8(values: Vector[Int]) -> String raises Utf8ConversionError
```

The exact error payload may add a reason code, but it must retain the first
failing byte index; an out-of-range `Vector[Int]` element must retain both its
index and value. The function names and argument/result types above are the
minimal Team A source contract. A scalar-to-String helper is derivable through
`moss.encoding` once this pair exists and is not a third request.

### Required semantics

- `string_utf8_bytes` returns an independent owned vector containing the
  exact UTF-8 bytes of the input `String`, each in `0..255`.
- `string_from_utf8` first validates every integer byte, then performs strict
  UTF-8 decoding. It rejects invalid leading/continuation bytes, truncation,
  overlong forms, surrogate encodings, and code points above `U+10FFFF`.
- Invalid input raises the typed first-error result. It never inserts a
  replacement character, drops bytes, returns a partial String, or uses an
  empty String as an error sentinel.
- Valid conversion is reciprocal and preserves embedded NUL, combining
  sequences, non-BMP values, and an empty String. It performs no normalization,
  case conversion, or locale-dependent transformation.
- Both directions create owned results. This request introduces no borrowed
  byte view or change to Moss String ownership.

### Required implementation parity

- **Checker:** resolve the concrete signatures and typed raise set. The
  outbound conversion is non-raising; the inbound conversion exposes only the
  declared `Utf8ConversionError` cases.
- **Native Debug and Release:** copy exact bytes in the outbound direction and
  construct a native String only after complete strict validation in the
  inbound direction.
- **Fast Debug:** match native values and first-error indices exactly, including
  embedded NUL and malformed input. It must not use replacement decoding.
- **ABI-v9:** record the signatures, ownership/effects, enum payloads, and raise
  set in the semantic interface. A source-free consumer must exercise success
  and recovered typed failures in Native Debug and Release.

### Acceptance vectors

Valid vectors must include:

| String/scalars | UTF-8 bytes |
| --- | --- |
| empty | `[]` |
| NUL | `[00]` |
| `A` / `U+0041` | `[41]` |
| `U+007F` | `[7f]` |
| `U+0080` | `[c2, 80]` |
| `U+07FF` | `[df, bf]` |
| `U+0800` | `[e0, a0, 80]` |
| `U+D7FF` | `[ed, 9f, bf]` |
| `U+E000` | `[ee, 80, 80]` |
| `U+FFFF` | `[ef, bf, bf]` |
| `U+10000` | `[f0, 90, 80, 80]` |
| `U+10FFFF` | `[f4, 8f, bf, bf]` |
| `aé🙂z` | `[61, c3, a9, f0, 9f, 99, 82, 7a]` |

Malformed vectors must include integer values `-1` and `256`, isolated
continuation `[80]`, overlong `[c0, 80]`, truncated `[e2, 82]`, surrogate
encoding `[ed, a0, 80]`, and above-maximum `[f4, 90, 80, 80]`. Validate exact
first-error indices and typed recovery in all execution/ABI modes listed above.

## Scope boundary

These requests do not include:

- general bitwise operators, unsigned integer types, a public packed-byte type,
  or changes to integer arithmetic;
- grapheme segmentation, Unicode normalization, case folding, locale collation,
  or a general String ordering policy;
- views, borrowing, zero-copy buffers, recursive values, dynamic dispatch, or
  changes to `READ`/`WRITE`/`CONSUME` semantics;
- a compiler implementation by Team A, an ABI version bump, CBOR tags/bignums,
  Float16/Float32 support, FlatBuffers, or Arrow IPC;
- any claim that the requested capabilities have landed or passed acceptance.

Team I may implement and review the two capabilities independently. Team A will
consume only an integrated source contract that passes checker, native, Fast
Debug, and ABI-v9 acceptance.

## Nonblocked Team A work

Team A continues all work that does not require these boundaries:

- `moss.binary` implements validated bytes, both endian orders, signed integer
  encodings, varints, length-delimited values, cursor atomicity, and limits
  using checked arithmetic rather than shifts.
- `moss.cbor` implements null, Bool, Int, byte strings, validated UTF-8 text
  stored as bytes/scalars, arrays, maps, iterative container ownership, exact
  lengths, malformed-input rejection, and resource limits.
- Until Request 1 lands, CBOR Float nodes and wire floats raise
  `CborError.FloatCapabilityUnavailable`; decimal conversion is forbidden.
- Until Request 2 lands, CBOR text remains lossless UTF-8 bytes/scalars and no
  native `String` adapter is advertised. String table ordering is either
  unsupported or implemented later from an accepted explicit ordering
  contract; no locale or byte-order behavior is invented.
- `moss.ndarray`, `moss.datastructures`, typed table operations not requiring
  String ordering, CSV work within its frozen contract, and cross-package
  integration that excludes gated adapters proceed independently.

Landing either request does not by itself approve Team A's SDK implementation.
The exact Team A implementation SHA still requires the frozen architecture's
package, interoperability, ABI-v9, negative-contract, and independent Architect
acceptance gates.
