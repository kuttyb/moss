# Phase 24 Team T — Text and Encoding architecture

**Fork point:** `610f2d632b53541cc31153174e37e1b1640d6f01`

**Feedback bootstrap merge:** `d62010e960f0db9b798a5b1ac37a9197d07b633d`

**Language/tool contract:** `moss-0.1`, `moss-agent-1`, compiler `0.1.0`

This note records the tested feasibility boundary used to design `moss.text`
and `moss.encoding`. It is based on bootstrap discovery, the canonical language
guides, semantic completion, structured diagnostics, and minimal native/Fast
Debug probes. Generated Rust was used only to confirm native execution or a
backend failure, never as the source of Moss semantics.

## Confirmed String and encoding surface

| Capability | Result | Evidence and consequence |
| --- | --- | --- |
| String code-point traversal | Supported | `length`, `char_at`, and `chars` agree in native and Fast Debug for `"aé🙂z"`; indices count Unicode code points. |
| Code-point slicing | Native primitive added by the performance sprint | `String.slice(start, length)` traverses UTF-8 boundaries without materializing one-String-per-scalar vectors, makes one result copy, and preserves the SDK invalid-bounds-to-empty contract in native and Fast Debug. |
| Efficient composition | SDK-feasible | Accumulate `Vector[String]` segments and call `"".join(parts)` once. There is no reserve/capacity API, mutable String, byte builder, or opaque public SDK type. |
| String UTF-8 bytes | Unsupported | Bootstrap and receiver completion expose no byte-view/conversion method; `text.bytes()` reports `TYPE_MISMATCH` for invalid builtin `string.bytes`. The performance-sprint `find`, `slice`, and ASCII case primitives preserve the String abstraction. |
| Unicode scalar integer to String | Unsupported | `chr`, `char`, and a static `String.from_codepoint` spelling are unresolved; `String` is not a value namespace. Existing literal code points can be recovered only through String traversal. |
| Byte vector to String | Unsupported | `string_from_utf8` and `String.from_utf8` are unresolved. The formal design explicitly leaves byte/text conversion semantics open. |
| Malformed UTF-8 in the pure codec | Supported as data | `Vector[Int]` represents untrusted bytes and typed `Utf8Error` cases carry byte/scalar indices and values. No replacement decoding is performed. |
| Invalid String indices | Built-in runtime precondition | `char_at(-1)` and `char_at(length)` check successfully, then fail at runtime; safe SDK APIs prevalidate instead of delegating invalid arguments. |
| Typed encoding failure | Supported | Closed enum payload errors check and execute in native and Fast Debug, including invalid byte index/value evidence. |

A separate adversarial probe found that a source file containing a raw invalid
UTF-8 byte inside a String literal can pass `moss check`, fail Fast Debug with an
invalid-UTF-8 String error, and produce Rust that `rustc` rejects. This is not a
String construction facility and Team T does not treat malformed Moss source as
a supported input path. It remains a compiler source-validation defect outside
the SDK implementation contract.

## Public architecture

### `moss.text`

All public indices and lengths are Unicode code-point units, never UTF-8 byte
offsets or grapheme clusters. The package provides:

- prefix/suffix tests and first/last substring search;
- exact safe slicing with an explicit invalid-bounds result;
- ASCII whitespace trim/tokenization and ASCII-only case conversion;
- non-overlapping replacement and safe split behavior for an empty separator;
- joining/concatenation helpers and a vector-backed segment builder.

The package deliberately does not claim normalization, locale-aware casing,
case-folding, collation, word segmentation, or grapheme segmentation. A
combining sequence may be sliced between scalar components.

Empty-pattern contracts are explicit: `find` returns zero, `rfind` returns the
input length, prefix/suffix tests succeed, and `replace_all` leaves the value
unchanged. Invalid exact slice bounds return `""`; `valid_slice` distinguishes
that result from a valid empty slice.

### `moss.encoding`

Layer A is a strict pure codec over Foundation byte buffers:

```text
Vector[Int] UTF-8 bytes  <->  Vector[Int] Unicode scalar values
```

Every byte is checked against `0..255`. Decoding rejects isolated continuation
bytes, invalid leading bytes, truncation, invalid continuations, overlong forms,
surrogates, and values above `U+10FFFF`. Encoding rejects negative values,
surrogates, and values above `U+10FFFF`. Typed `Utf8Error` cases preserve the
first relevant input index and offending value or sequence facts.

Layer B (Moss String interoperability) is deferred because both directions of
the required runtime boundary are absent. The pure codec is complete without
that layer; no incomplete String conversion is advertised.

## Smallest String interoperability feature request

The smallest useful extension is an explicit, owned, strict conversion pair:

```text
string_utf8_bytes(value: String) -> Vector[Int]
string_from_utf8(values: Vector[Int]) -> String raises Utf8ConversionError
```

The first operation returns an independent byte vector. The second validates
strict UTF-8 and reports a typed first-error byte index; it must not replace
malformed input silently. The conversion names are illustrative design inputs,
not currently accepted Moss syntax. A scalar constructor could be derived
through UTF-8 encoding once this pair exists, so it is not required as a third
primitive. This request does not add grapheme, normalization, or locale policy.

## Task DAG and ownership

```text
A: feedback bootstrap + feedback-enabled fork
                    |
                    v
B: feasibility and API contract
          /---------+----------\
          v         v          v
   T1 text ops   T2 builder   E1 pure UTF-8
          \         |          /
           \--------+---------/
                    v
 C: combined external consumer + ownership/source-free validation
                    |
          /---------+----------\
          v                    v
 D1 Python oracle         D2 performance/allocation
          \                    /
           \------------------/
                    v
 E: feedback synthesis + independent exact-SHA Architect verdict
                    |
                    v
 F: publish phase24-text for Team I
```

String interoperability is a documented blocked edge off B, not a dependency
of E1 or the pure-codec delivery. Optional ASCII/hex/Base64 expansion is lower
priority than the validation, ABI, and performance gates and is not required to
declare the strict UTF-8 layer complete.

## Adversarial validation contract

The permanent and generated corpora cover:

- scalar boundaries `0`, `7F`, `80`, `7FF`, `800`, `D7FF`, `E000`, `FFFF`,
  `10000`, and `10FFFF`;
- NUL, ASCII, combining marks, CJK, emoji, empty and large buffers;
- byte integers outside `0..255`, lone continuations, invalid leading bytes,
  every sequence truncation width, overlong encodings, encoded surrogates, and
  values above `U+10FFFF`;
- exact encode/decode round trips against Python's strict UTF-8 codec;
- empty/whole/absent/overlapping text matches, empty replacement, combining
  boundaries, ASCII versus non-ASCII whitespace, and repeated builder use;
- native debug, native release applications, Fast Debug tests, external path
  consumers, and compiled-only ABI-v9 providers where supported.

Static `moss cost` output is not treated as allocation proof: the current query
can report no materialized functional intermediates for a manual slice that
still allocates character and result vectors. Builder and codec claims therefore
use measured release workloads and explicit algorithm structure.
