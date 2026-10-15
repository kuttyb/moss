# Phase 25 — Team S Design and Acceptance

**Branch:** `phase25-semantics-engines`
**Pinned baseline:** `fa3a340a0a6652568b97869489da1a84038b9857`
**Coordinator:** L6 Team S
**Independent Architect:** L6 (to be spawned)

## Status: PLANNING

---

## 1. G0 — Joint Architecture Proofs

### Proof A1: Provider-owned generic Rust representation

**Claim:** Provider-owned generic types generate one Rust struct per provider
package. Consumer free-function methods on those types are generated as
standalone `fn` definitions in the consumer crate, using the provider's concrete
Rust type. This applies to ordinary value methods, not domain handlers or FileIO.

**Strategy:** Generate a minimal two-consumer fixture:
- Provider defines `type Pair` with fields `first: Int, second: Int`
- Consumer A defines `fn sum(p: Pair) -> Int`
- Consumer B defines `fn product(p: Pair) -> Int`
- Both consumers compile and link against the provider's `.mossi` + native artifact

**Status:** Pending joint authoring with Team G.

### Proof A2: Static Map/Set key dispatch with custom isequal/hash

**Claim:** A user-defined type in a source-free provider with custom `isequal`
and `hash` can serve as a Map key. Built-in Map/Set operations use those Moss
functions, not Rust's native `Eq`/`Hash`.

**Implementation strategy options (to be decided in G0):**
1. **Newtype wrapper**: `struct MossKey<T>(T)` with custom `Eq`/`Hash` impl
   that calls Moss-generated functions — avoids orphan rule
2. **Generated adapters**: Per-key-type `impl Eq/Hash` on a generated wrapper
3. **Custom hash map**: Moss-owned HashMap that doesn't use Rust traits

**Float key problem:** IEEE `f64` is not `Eq` or `Hash` in Rust. Julia-like
total ordering (NaN last, -0 < +0) with Moss `isequal` (NaN == NaN, -0 != +0)
requires a wrapper type. This wrapper must implement Rust `Eq + Hash` for
HashMap but with Moss semantics.

**Rust orphan restrictions:** Cannot `impl Hash for f64`. Must use newtype or
wrapper. The newtype must be zero-cost (repr(transparent)).

**Status:** Pending — requires joint resolution with Team G.

### Proof A3: Target capability preflight

**Claim:** `moss check` produces a target-capability diagnostic before execution
when a program uses features unsupported by the target engine.

**Current state:** Fast Debug already rejects:
- `FAST_DEBUG_NATIVE_DEPENDENCY` for source-free providers
- FileIO (native-only)
- `for` traversal (documented limitation, throws at runtime — defect #8)

**Required behavior:** Static preflight should reject before interpreter startup
for known unsupported constructs, not throw at runtime.

**Status:** Design pending.

### Proof A4: Semantic result comparison model for differential CI

**Claim:** Differential CI compares program values across engines using a
semantic oracle that normalizes:
- Map iteration order (unspecified)
- Float string formatting (within specified tolerance)
But does NOT normalize:
- Incorrect numeric precision
- Wrong computed values
- Missing output lines

**Comparison model:**
1. Run program under native debug, native release, and Fast Debug
2. Capture stdout lines
3. Parse output values into typed representations
4. Compare with type-aware equality: Int exact, Float ULP-bounded, String exact,
   Map key-set + value equality (order-independent), Vector ordered

**Status:** Design ready for joint approval.

### Proof A5: Char representation and String ABI-v10

**Claim:** Char is a distinct scalar type representing one valid Unicode scalar
value (0x0000–0xD7FF, 0xE000–0x10FFFF).

**Representation:**
- Internal: `u32` (or `i64` for Moss Int compatibility, TBD)
- Literal syntax: `'a'`, `'\n'`, `'\u{1F600}'` (TBD — must not conflict)
- Constructor: `Char(codepoint: Int)` raising on invalid scalar
- Conversion: `char.to_string()`, `char.codepoint()` -> Int

**String metadata (ABI-v10):**
- `len_codepoints: i64` — eager code-point count
- `is_ascii: bool` — flag maintained by all producers
- `sparse_offsets: Vec<(i64, usize)>` — for non-ASCII strings ≥64 codepoints,
  byte offsets at each 64-codepoint boundary

**Metadata maintenance rules:**
- `append(s)`: update `len_codepoints += s.len_codepoints`,
  `is_ascii = is_ascii && s.is_ascii`, recompute sparse offsets for final string
- `reserve(n)`: no content change, no metadata change
- `slice(start, len)`: recount or derive from sparse offsets
- Cross-module transfer: metadata travels with the String

**ABI-v10 migration:** Old ABI-v9 String (raw `std::string`) is incompatible.
Source-free providers must be rebuilt. Version diagnostic mandatory.

**Status:** Design pending joint approval.

### Proof A6: Benchmark methodology

**Claim:** Benchmarks use:
- `margo bench` with release profile
- At least 4 runtime-varying input sizes
- Wall time measured by Rust `criterion` or manual timing
- Memory allocation counts where possible
- Comparative baseline doing equivalent work with release-optimized providers

**Avoid:** Folded/static work, incomparable baselines, unvalidated ratios.

**Status:** Ready for joint approval.

---

## 2. G1 — Parity and Correctness Backlog

### Defect Analysis

| ID | Root Cause Location | Fix Strategy |
|---|---|---|
| #1 | `interpreter.hpp:1303-1304` — Int→double conversion | Use int64 comparison when both operands are Int |
| #2 | `moss.cpp` checker — generic </> | Add checker rejection for unsupported types |
| #3 | Codegen — reassignment lowering | Fix binding tracking in native code generation |
| #4 | `moss.cpp` codegen — Rust borrow conflict | Split mutable/immutable borrows in generated code |
| #5 | Codegen — `_ = f()` discard | Generate `let _ = ...` in Rust |
| #6 | Codegen — assert format strings | Escape `{` and `}` in generated Rust format! |
| #7 | `interpreter.hpp` — Float display | Use same formatting as native Rust f64 Display |
| #8 | `interpreter.hpp:1834` — for loops | Implement for-loop traversal in interpreter |
| #10 | `moss.cpp` — name resolution | Fix constructor vs import precedence |
| #12d | `moss.cpp` — member dispatch | Fix local collection method call dispatch |
| #14 | `moss.cpp` codegen — benchmark raise | Fix Phase 21 outcome lowering in bench bodies |
| #15 | `moss.cpp` — local field method | Fix `h.data.push(x)` local member dispatch |

### Differential CI Design

Test harness: Python script that:
1. Runs `moss check <fixture>.moss --json` — must pass
2. Runs `margo build` (native debug) — must pass
3. Runs `margo --release build` (native release) — must pass
4. Runs `moss run --interp <fixture>.moss` (Fast Debug, where supported) — captures output
5. Runs native debug binary — captures output
6. Runs native release binary — captures output
7. Compares outputs using semantic oracle

Records: command, toolchain version, profile, input, expected/actual, compiler SHA.

---

## 3. G4 — String, Char, UTF-8

### Implementation Plan

1. **Char type system** (checker): Add `Char` as a new built-in scalar type
2. **Char operations**: equality, ordering, hash, to_string, codepoint
3. **String metadata struct**: Add to String representation in codegen
4. **String producer/mutator audit**: All paths maintain metadata
5. **char_at() return type change**: String → Char
6. **chars() return type change**: Vector[String] → Vector[Char]
7. **ABI-v10 versioning**: Add version check, reject old providers

### Benchmark Fixtures

- 1 MB ASCII JSON cursor parse
- 1 MB non-ASCII (CJK/emoji) JSON cursor parse
- Repeated char_at() across string lengths: 100, 1K, 10K, 100K
- String append benchmark: 1K, 10K, 100K, 1M characters

---

## 4. G3 — Ordering, Equality, Hashing

### Semantic Contracts

**Int.less(a, b):** `a < b` (standard signed integer comparison)
**Float.less(a, b):** Julia-like `isless`: NaN last, -0 < +0
**String.less(a, b):** Byte-wise UTF-8 comparison (= Unicode scalar lexicographic)
**Char.less(a, b):** Scalar value comparison

**Float.isequal(a, b):** NaN == NaN (any bit pattern), -0 ≠ +0
**Float == (IEEE):** NaN ≠ NaN, -0 == +0 (unchanged)

**Map/Set keys:** Use `isequal`/`hash`, not `==`
**Heap/Sort:** Use `less`

### Law Tests Required

- Irreflexive: `not less(x, x)`
- Asymmetric: `less(x, y) implies not less(y, x)`
- Transitive: `less(x, y) and less(y, z) implies less(x, z)`
- Total: `less(x, y) or less(y, x) or isequal(x, y)`
- Reflexive equality: `isequal(x, x)`
- Symmetric equality: `isequal(x, y) implies isequal(y, x)`
- Transitive equality: `isequal(x, y) and isequal(y, z) implies isequal(x, z)`
- Hash consistency: `isequal(x, y) implies hash(x) == hash(y)`

---

## 5. Exit Criteria

### G1 Exit
- [ ] Each defect has permanent positive/negative regression
- [ ] Regressions pass in native debug, native release, Fast Debug (where supported)
- [ ] Full 64-bit test matrix passes
- [ ] Differential CI operational
- [ ] Unsupported target features rejected before execution

### G4 Exit
- [ ] Char and String ABI fixtures pass
- [ ] All conversions defined and tested
- [ ] Random Unicode tests pass
- [ ] Benchmarks show expected scaling with measured memory overhead
- [ ] Text packages have migration guidance

### G3 Exit
- [ ] Map/Set with imported custom key passes
- [ ] Float key semantics correct
- [ ] Heap/sort use less
- [ ] No runtime dispatch
- [ ] All law tests pass
- [ ] Reverse and by: compile source-free with purity enforcement

### Final Submission
- [ ] Branch pushed upstream with reviewed SHA
- [ ] This document complete with test commands/results
- [ ] Comparator/key contract document
- [ ] Unicode/Char migration note
- [ ] Full 64-bit audit
- [ ] Benchmark methodology/data
- [ ] Structured agent feedback
- [ ] Independent L6 Architect verdict
- [ ] Phase 25.5 D1/D2 follow-up items listed
