# Team D Independent Architect — Design Challenge & Evaluation Criteria

**Phase:** Phase 24 — Structured Data SDK (`moss.parse`, `moss.fmt`, `moss.json`, `moss.csv`)  
**Role:** Independent L6 Architect  
**Status:** ACTIVE CHALLENGE / CRITERIA DEFINED  

---

## 1. Architectural Mandates & Challenges

As the independent L6 Architect, I challenge Team D's proposed designs across five critical axes:

### A. Non-Recursive Indexed Arena / Tape Architecture (`moss.json`)
- **Challenge:** Can the JSON AST be cleanly expressed without recursive data types or heap-allocated tree pointers in Moss v0.1?
- **Finding:** Moss compiler strictly rejects recursive enums and recursive aggregate types (`recursive enum or aggregate layout involving 'JsonValue' is unsupported`).
- **Architectural Requirement:**
  - Team D must implement an **Indexed Arena / Tape** representation (`JsonDoc` / `JsonTape`).
  - Node kinds (`Null`, `Bool`, `Number`, `String`, `Array`, `Object`) must be mapped to compact node records indexed by integer IDs.
  - Arrays and Objects must store contiguous slice ranges or child index vectors.
  - The parser MUST NOT use recursive function calls (Moss does not support ordinary recursion in v0.1). It must use an explicit stack vector `Vector[Int]` to traverse nested objects and arrays iteratively.
  - Maximum nesting depth must be guarded against adversarial deep inputs (e.g. 500+ brackets).

### B. Feasibility Gate: Unicode Scalar-to-String Decoding
- **Challenge:** How will JSON string escape sequences (`\uXXXX` and surrogate pairs `\uD83D\uDE00`) be handled given the absence of a scalar-to-String runtime primitive?
- **Finding:** Team T established in `sdk/encoding/README.md` and `CURRENT_STATUS.md` that Moss v0.1 has no native primitive to construct a `String` from an integer Unicode scalar.
- **Architectural Requirement:**
  - Full compliance with the prompt mandate: **DO NOT claim compliance** if arbitrary `\uXXXX` cannot be converted to Strings.
  - All `\uXXXX` and surrogate pairs must be fully validated and decoded to integer Unicode scalars (`Vector[Int]` or integer scalar attributes).
  - ASCII escapes (`\u0000` through `\u007F`) must be decoded to native 1-character strings.
  - Non-ASCII escapes must be cleanly surfaced in the Tape representation (preserving scalar value) and raise an explicit typed error `JsonError.UnsupportedUnicodeEscape(line, col, scalar)` if string synthesis is requested.
  - The escalation document `docs/phase24/TEAM_I_ESCALATION_UNICODE_SCALAR.md` and integration issue `P24-D-001` must remain open until Team I provides the compiler primitive.
  - Absolutely **NO edits** to compiler internals, lowering, or numerical packages.

### C. RFC 4180 CSV Correctness (`moss.csv`)
- **Challenge:** Many naive CSV parsers fail on embedded newlines, trailing empty lines, uneven fields, or CRLF line endings.
- **Architectural Requirement:**
  - Strict RFC 4180 parsing:
    - Quoted fields may contain commas, line breaks (`\n` and `\r\n`), and escaped quotes `""`.
    - Double quotes within a quoted field are represented by a pair of consecutive double quotes.
    - Spaces are considered part of a field and not silently trimmed unless requested.
    - Support configurable delimiters (`,` `\t` `;` `|`).
    - The writer must quote fields containing the delimiter, newlines, or double quotes, and properly escape quotes.
  - Parser must track row/column source positions and report `CsvError` on malformed inputs (e.g. unclosed quote at EOF).

### D. Cursor & Position Tracking (`moss.parse`, `moss.fmt`)
- **Architectural Requirement:**
  - `moss.parse` must provide a reusable, high-performance cursor abstraction tracking character offsets, 1-based lines, and 1-based columns.
  - `moss.fmt` must provide robust JSON string escaping (including `\"`, `\\`, control codes `\b`, `\f`, `\n`, `\r`, `\t`, and `\u00XX`), CSV field escaping, and pretty-printing indentation.

### E. Verification & Benchmark Methodology (Addressing Team M/T Historical Gaps)
- **Challenge:** Teams M and T suffered from benchmark methodology issues (P24-T-001 debug providers, P24-T-002 mismatched workloads, P24-M-005 nonportable harnesses, P24-M-006 constant folding).
- **Architectural Requirement:**
  - **No nonportable or source-mutating harnesses:** All differential test runners and benchmark harnesses must be fully self-contained and run cleanly from repository paths.
  - **Identical Workloads:** The Moss benchmark and Rust benchmark (`bench_baseline.rs`) must process the exact same byte-for-byte input payloads.
  - **Release Optimization:** Benchmarks must be built with `--release`.
  - **No Dead-Code Elimination / Constant Folding:** All benchmark results must be observed (e.g., checksums, total node counts, or byte counts summed and checked).
  - **Multi-Engine Validation:** Every package must pass Native Debug, Native Release, Fast Debug (`--interp`), and compiled-only ABI-v9 execution.

---

## 2. Review Gate Checklist for Architect GO
1. `moss.parse` unit and position tests pass in native and Fast Debug.
2. `moss.fmt` escaping and formatting tests pass in native and Fast Debug.
3. `moss.json` arena representation, tokenizer, and iterative parser pass in native and Fast Debug.
4. `moss.csv` RFC 4180 compliance tests pass across delimiter, quote, CRLF, and embedded newline variations.
5. Independent differential tests comparing against standard Python `json` and `csv` modules pass 100% without divergence.
6. Adversarial suite (deep nesting, empty inputs, malformed characters, boundary cases) passes cleanly.
7. Compiled-only ABI-v9 provider builds verify source-free execution.
8. Full repository regressions pass (`TMPDIR="$PWD/tmp" make check`, `make examples`).
9. Clean git diff (`git diff --check`).
10. Measured performance comparisons against Rust baseline.
