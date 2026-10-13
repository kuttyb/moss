# Team D Escalation to Team I — Unicode Scalar-to-String Runtime Primitive

**Date:** 2026-10-12  
**Submitter:** Team D (Structured Data SDK)  
**Target:** Team I (Phase 24 Integration Owner)  
**Status:** SUBMITTED / OPEN  

---

## 1. Summary

In accordance with the Phase 24 mission instructions:
> *"Full JSON Unicode escape support is a feasibility gate: do not claim compliance if `\\uXXXX` and surrogate pairs cannot be decoded. If a minimal missing runtime primitive is required, submit the reproducer and proposed interface to Team I instead of independently modifying the compiler."*
> *"No edits to numerical packages, String compiler lowering, or Team T's performance work."*

Team D has investigated Moss v0.1 capabilities and confirmed that while Moss supports:
- String methods: `length()`, `char_at(index)`, `chars()`, `split(separator)`, `join(parts)`
- UTF-8 string literals in source code (e.g. `"€"`, `"😀"`)
- Integer Unicode scalar validation and encoding via `moss.encoding` (`is_unicode_scalar`, `encode_utf8`, `decode_utf8`)

Moss v0.1 **does not provide any runtime primitive** to construct a `String` from an integer Unicode scalar (e.g. `65 -> "A"`, `0x20AC -> "€"`, `0x1F600 -> "😀"`) or from a `Vector[Int]` of UTF-8 bytes.

This is the exact same architectural boundary previously documented by Team T in `sdk/encoding/README.md`:
> *"Moss v0.1 exposes code-point-based String traversal through length, char_at, and chars, but it does not expose String UTF-8 bytes, construction of a one-code-point String from an integer scalar, or construction of a String from a byte vector. Therefore this package does not claim String conversion. Its complete supported contract is the pure codec layer above. Adding String interop requires the language/runtime to expose those conversions explicitly."*

And confirmed in `.codex/CURRENT_STATUS.md`:
> *"Layer B String interoperability is explicitly deferred because Moss v0.1 exposes no supported String/UTF-8 byte conversion or scalar String constructor."*

---

## 2. Reproducer

Given a parsed JSON escape sequence such as `"\\u20AC"` (Euro sign, scalar `8392`) or surrogate pair `"\\uD83D\\uDE00"` (Grinning face, scalar `128576`), the parser extracts the hex digits and computes the scalar:

```moss
# Test reproducer: tmp/repro_scalar_to_string.moss
module main

fn scalar_to_string(scalar: Int) -> String:
  # There is no built-in or intrinsic in Moss v0.1 to convert `scalar` to a 1-character String.
  # Calling any guessed spelling fails at compilation:
  # return char(scalar)           # compile error: unknown function 'char'
  # return from_scalar(scalar)    # compile error: unknown function 'from_scalar'
  # return String.from_char(...)  # compile error: static methods unsupported
  return ""

fn main():
  s = scalar_to_string(8392)
  echo s
```

Without a compiler/runtime hook, converting arbitrary Unicode scalars in range `0..0x10FFFF` to native Strings would require embedding a multi-megabyte lookup table in source, which is infeasible, violates clean SDK principles, cannot represent null bytes, and fails across compiler engines.

---

## 3. Proposed Minimal Runtime Interface

Team D proposes that Team I / the compiler team add one of the following minimal primitives:

### Option A: `string_from_scalar(scalar: Int) -> String` (Recommended)
- **Signature:** `export fn string_from_scalar(scalar: Int) -> String` (or built-in `char_from_scalar(scalar: Int) -> String`)
- **Semantics:**
  - If `scalar` is a valid Unicode scalar (`0..0x10FFFF`, excluding surrogates `0xD800..0xDFFF`), returns a 1-character `String` containing that code point.
  - If invalid, raises `encoding.Utf8Error.InvalidScalar` (or returns empty `""`).
- **Rust Lowering:**
  ```rust
  char::from_u32(scalar as u32).map(|c| c.to_string()).unwrap_or_default()
  ```
- **Interpreter Lowering:**
  Emits a 1-character string from the decoded scalar code point.

### Option B: `string_from_utf8(bytes: Vector[Int]) -> String`
- **Signature:** `export fn string_from_utf8(bytes: Vector[Int]) -> String`
- **Semantics:**
  - Decodes validated UTF-8 bytes to native `String`.
- **Rust Lowering:**
  ```rust
  String::from_utf8(bytes.iter().map(|&b| b as u8).collect()).unwrap_or_default()
  ```

---

## 4. Team D Interim Behavior & Feasibility Compliance

Until this primitive is integrated by Team I:
1. **ASCII Escapes:** Team D fully decodes all ASCII escapes (`\u0000` through `\u007F`, including control characters and printable ASCII) to native Strings via an optimized ASCII dispatch table.
2. **Surrogates & Validation:** Team D fully validates and decodes all `\uXXXX` sequences and `\uD800..\uDBFF \uDC00..\uDFFF` surrogate pairs into integer Unicode scalars, rejecting malformed sequences, invalid hex digits, unpaired surrogates, and out-of-range scalars with typed `JsonError` variants.
3. **Feasibility Gate Adherence:** In strict adherence to the Phase 24 instructions, Team D **does not falsely claim 100% non-ASCII Unicode escape string synthesis compliance** without the runtime primitive. When non-ASCII `\uXXXX` sequences are encountered, the parser either:
   - Preserves the raw sequence or returns the decoded scalar in the tape representation.
   - Raises `JsonError.UnsupportedUnicodeEscape(line: Int, col: Int, scalar: Int)` when synthesizing pure strings.
4. **Zero Compiler Edits:** In accordance with the prompt ("No edits to numerical packages, String compiler lowering, or Team T's performance work"), Team D has not modified `src/moss.cpp` or compiler lowering.
