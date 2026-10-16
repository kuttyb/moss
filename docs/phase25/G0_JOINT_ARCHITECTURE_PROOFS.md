# Phase 25 — G0 Joint Architecture Proofs: Semantics & Engines

**Authors:** Team S L6 Coordinator, Team S L6 Independent Architect  
**Co-Authors:** Team G L6 Coordinator, Team G L6 Architect  
**Approved Baseline:** `fa3a340a0a6652568b97869489da1a84038b9857`  
**Status:** JOINT ARCHITECTURE PROOF COMPLETE — APPROVED FOR G1/G4

> **Superseded in part (2026-10-15, L7 review of `983acb2`).** Proof 2's
> type-owned `MossKey_T` and `MossFloatKey` adapters are withdrawn: the
> canonical design is Team G's policy-free, compiler-owned `MossMap<K,V>`
> under `key-protocol-v1` (`docs/phase25/TYPE_IR_CONTRACT.md`, L7 G0 GO at
> `fb2d273`). Proof 5's "ABI-v10 bump" is not delivered by changing the
> native ABI number; ABI v10 is one coordinated Team G/Team S/L7 transition.
> Proof 3's Fast Debug FileIO preflight and Proof 5's String record are now
> implemented as described in `docs/phase25/TEAM_S_L7_REPAIR_REPORT.md`. The
> original text below is preserved as review history.

---

## Proof 1: Provider-Owned Generic Rust Representation and Consumer Free-Function Lowering

### 1.1 Theorem
Let $P$ be a provider package defining a value type $T$. Multiple independent consumer packages $C_1$ and $C_2$ may define value methods on $T$ without:
1. Violating Rust orphan rules (`E0117`, `E0210`);
2. Inducing name or symbol collisions across consumer crates;
3. Requiring runtime dispatch, vtables, or dynamic trait objects;
4. Precluding future hidden-field access control.

Furthermore, this rule applies exclusively to ordinary value methods; it does NOT apply to domain handlers (which require compiler-planned two-phase locking and static message routing) or `FileIO` (which has pinned linear ownership).

### 1.2 Lowering Model
1. **Provider Lowering:**
   In provider crate $P$, the nominal type $T$ is emitted as a concrete Rust struct:
   ```rust
   // Emitted in crate P (libprovider.rlib)
   #[derive(Clone, Debug)]
   pub struct Point {
       pub x: i64,
       pub y: i64,
   }
   ```
2. **Consumer Free-Function Lowering:**
   In consumer crate $C_1$, an ordinary method `fn norm_squared(self) -> Int` on `Point` is lowered not as an inherent `impl Point` (which Rust forbids across crate boundaries) and not as an orphan trait `impl Trait for Point`, but as an export-qualified free function:
   ```rust
   // Emitted in crate C1
   pub fn moss__fn__norm_squared(this: &libprovider::Point) -> i64 {
       this.x * this.x + this.y * this.y
   }
   ```
3. **Dispatch:**
   The Moss checker resolves `p.norm_squared()` statically during call-target resolution to `C1::norm_squared`. Codegen emits `moss__fn__norm_squared(&p)`.
   In consumer crate $C_2$, another method `fn manhattan(self) -> Int` is lowered to `C2::moss__fn__manhattan(&p)`.
4. **Future Hidden-Field Access:**
   When field visibility (private/package-private) is introduced in a future phase, provider $P$ emits getter/accessor functions (`pub fn get_x(&self) -> i64`) or crate-internal modules; consumer methods operate via the provider's public visibility interface without altering the consumer free-function lowering architecture.
5. **Boundary Confirmation:**
   Domain handlers communicate strictly via synchronous `message domain.Handler(args...)` and receive compiler-derived `(domain_rank, class_rank)` lock ordering. Value methods on ordinary structs have no locks, no handlers, and no routing capabilities.

---

## Proof 2: Static Map/Set Key Dispatch, Custom `isequal`/`hash`, and Float Total Order

### 2.1 The Problem
In Moss, `Map[K, V]` and `Set[K]` require key equality and hashing.
Native Rust `std::collections::HashMap<K, V>` imposes two blockers:
1. **Rust Orphan Rule (`E0117`):** If a user defines type `UserKey` in package $P$, and package $C$ builds `Map[UserKey, V]`, package $C$ cannot write `impl std::hash::Hash for UserKey` or `impl std::cmp::PartialEq for UserKey`.
2. **Missing Float `Eq`/`Hash`:** Rust's standard `f64` deliberately implements neither `Eq` nor `Hash` due to IEEE-754 `NaN != NaN` and `-0.0 == +0.0`. Rust's `f64::total_cmp` cannot be used directly as a substitute because it places negative NaNs first (`-NaN < -inf < ... < +NaN`), violating the Moss language specification which mandates Julia-like `isless` (`finite < NaN`) and `isequal(NaN, NaN) == true` with distinct zeros (`isequal(-0.0, +0.0) == false`).

### 2.2 Selected Strategy: Type-Owned Transparent Key Adapters (`MossKey<T>`)
To achieve zero-cost static dispatch without orphan violations or dynamic trait objects:

1. **Provider-Emitted Key Adapter:**
   For every user-defined type $T$ declared in a provider $P$, the compiler generates a dedicated key wrapper struct inside crate $P$:
   ```rust
   // Generated inside crate P where T is local
   #[repr(transparent)]
   #[derive(Clone, Debug)]
   pub struct MossKey_T(pub T);

   impl PartialEq for MossKey_T {
       #[inline]
       fn eq(&self, other: &Self) -> bool {
           // Calls Moss-specialized isequal
           moss__fn__isequal(&self.0, &other.0)
       }
   }

   impl Eq for MossKey_T {}

   impl std::hash::Hash for MossKey_T {
       #[inline]
       fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
           let h = moss__fn__hash(&self.0);
           state.write_u64(h as u64);
       }
   }
   ```
   Because `MossKey_T` and `T` are both defined in crate $P$, `impl PartialEq`, `impl Eq`, and `impl Hash` for `MossKey_T` are 100% compliant with Rust orphan rules.
2. **Built-in Float Key Adapter (`MossFloatKey`):**
   In the compiler runtime/prelude, `Float` keys are wrapped in `MossFloatKey`:
   ```rust
   #[repr(transparent)]
   #[derive(Copy, Clone, Debug)]
   pub struct MossFloatKey(pub f64);

   impl PartialEq for MossFloatKey {
       #[inline]
       fn eq(&self, other: &Self) -> bool {
           if self.0.is_nan() && other.0.is_nan() {
               return true; // Any NaN is equal to any NaN under isequal
           }
           if self.0 == 0.0 && other.0 == 0.0 {
               // Distinguish -0.0 and +0.0: check sign bit
               return self.0.to_bits() == other.0.to_bits();
           }
           self.0 == other.0
       }
   }

   impl Eq for MossFloatKey {}

   impl std::hash::Hash for MossFloatKey {
       #[inline]
       fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
           if self.0.is_nan() {
               // All NaNs hash to canonical NaN bit representation
               state.write_u64(0x7ff8000000000000u64);
           } else if self.0 == 0.0 {
               // Preserve sign bit of zero: +0.0 != -0.0
               state.write_u64(self.0.to_bits());
           } else {
               state.write_u64(self.0.to_bits());
           }
       }
   }
   ```
3. **Float Total Ordering (`Float.less` / Julia `isless`):**
   The scalar function `Float.less(a, b)` implements exact Julia `isless`:
   ```rust
   #[inline]
   pub fn moss_float_less(a: f64, b: f64) -> bool {
       if a.is_nan() {
           return false; // NaNs are sorted last
       }
       if b.is_nan() {
           return true;  // Any non-NaN is less than NaN
       }
       if a == 0.0 && b == 0.0 {
           // -0.0 is ordered before +0.0
           return (a.to_bits() >> 63) > (b.to_bits() >> 63);
       }
       a < b
   }
   ```
   **Mathematical Mapping Proof:**
   - *Irreflexivity:* $\forall x, \neg(x < x)$. If $x$ is NaN, returns false. If $x=0$, bits are equal, returns false. If finite, $x < x$ is false.
   - *Asymmetry:* $x < y \implies \neg(y < x)$. If $y$ is NaN and $x$ finite, $x < y$ is true and $y < x$ is false. If $x=-0.0, y=+0.0$, $x < y$ is true, $y < x$ is false.
   - *Transitivity:* $x < y \land y < z \implies x < z$. Partition into $[-\infty, -0.0] < [+0.0, +\infty] < \{\text{NaN}\}$. Real order holds on negatives and positives. $-0.0 < +0.0$ bridges them. All finite values $< \text{NaN}$. Transitivity holds globally.
   - *Total Comparability:* $\forall x, y$, exactly one of $x < y$, $y < x$, or $\text{isequal}(x, y)$ holds.

4. **Lowered Map/Set Representation:**
   `Map[K, V]` lowers to:
   - If $K = \text{Int}$: `std::collections::HashMap<i64, V>`
   - If $K = \text{String}$: `std::collections::HashMap<MossStringKey, V>`
   - If $K = \text{Float}$: `std::collections::HashMap<MossFloatKey, V>`
   - If $K$ is user type $T$: `std::collections::HashMap<MossKey_T, V>`
   Lookup `map[k]` wraps the key transparently as `MossKey_T(k)`. Because `#[repr(transparent)]` guarantees identical layout, this has zero runtime overhead.

---

## Proof 3: Target Capability Preflight

### 3.1 Principle
A target-accepted program must execute with authoritative Moss semantics. If a target execution engine lacks support for a capability demanded by the checked program, the compiler must reject the program **prior to execution** with a structured capability diagnostic, rather than failing mid-run or silently diverging.

### 3.2 Preflight Rules
1. **Fast Debug (Interpreter):**
   - Rejects `FileIO` at preflight before running `main`: diagnostic `FAST_DEBUG_FILEIO: FileIO requires native execution`.
   - Rejects source-free compiled `.mossi` without available source at preflight: diagnostic `FAST_DEBUG_NATIVE_DEPENDENCY`.
   - `for` loops: natively supported in Phase 25 (Defect #8 closure).
2. **Native Debug & Release Targets:**
   - Authoritative execution of all language features, including FileIO, multi-domain concurrent topologies, and source-free provider libraries.
3. **Execution Invariant:**
   For any program $P$ and target $E$:
   $$\text{Check}(P) = \text{Ok} \implies \left( \text{Preflight}(P, E) = \text{Ok} \implies \text{Exec}(P, E) \text{ conforms to Moss semantics} \right)$$
   $$\text{Preflight}(P, E) = \text{Err}(C) \implies \text{Execution does not start; returns exit code } 1 \text{ with diagnostic } C$$

---

## Proof 4: Semantic Result Comparison Model for Differential CI

### 4.1 Comparison Axioms
Differential CI executes programs across multiple targets (Native Debug, Native Release, Fast Debug) and compares observable outcomes using a typed semantic oracle:

1. **Permissible Non-Determinism (Map Traversal):**
   In Moss v0.1, `map.keys()` and `map.values()` produce eager vectors whose order is explicitly unspecified. The differential comparison oracle parses serialized map outputs and sorts dictionary keys before asserting equality.
2. **Strict Integer Parity (No Silenced Loss):**
   Signed 64-bit integer values in $[-2^{63}, 2^{63}-1]$ must be represented and compared exactly across all engines. The Fast Debug interpreter must never truncate or approximate integer arithmetic or comparison via IEEE-754 `f64` (Defect #1 closure).
3. **Floating-Point Parity:**
   `Float.to_string()` output must match the native platform display representation exactly across engines (Defect #7 closure).
4. **Error & Signal Equivalence:**
   If a program raises an error or panics on an assertion, all engines must raise equivalent error variants and failure status.

---

## Proof 5: Char Representation, String Metadata, and ABI-v10 Migration

### 5.1 Char Representation
1. `Char` is a first-class scalar type representing a valid Unicode scalar value:
   $$\text{Char} \in [0x0000, 0xD7FF] \cup [0xE000, 0x10FFFF]$$
2. In Rust native lowering: `pub type MossChar = char;` (stored as 32-bit `u32`).
3. In Fast Debug interpreter: `Value::Kind::Char` with `uint32_t codepoint`.
4. Scalar literal syntax: `'a'`, `'\n'`, `'\u{1F600}'`.
5. Constructor `Char(codepoint: Int) -> Char`: raises `ValueError` if out of scalar range or in surrogate range.
6. Methods on `Char`:
   - `char.to_string() -> String`: returns 1-character String.
   - `char.codepoint() -> Int`: returns integer code point in $[0, 0x10FFFF]$.
   - `Char.less(a, b)`: numeric comparison of code points.
   - `Char.isequal(a, b)`: exact equality of code points.
   - `Char.hash(a)`: integer hash of code point.

### 5.2 String Metadata Maintenance (ABI-v10)
1. **Data Structure:**
   ```rust
   pub struct MossString {
       pub bytes: String,              // UTF-8 payload
       pub len_codepoints: usize,      // Total Unicode code points
       pub is_ascii: bool,             // True if all bytes < 128
       pub sparse_offsets: Vec<u32>,   // Byte offsets at codepoint indices 64, 128, 192, ...
   }
   ```
2. **Metadata Invariants:**
   - `is_ascii == true`: `len_codepoints == bytes.len()`, `sparse_offsets.is_empty()`.
   - `len_codepoints < 64`: `sparse_offsets.is_empty()`.
   - `len_codepoints >= 64 && !is_ascii`: `sparse_offsets[k]` stores the byte offset of code point $(k + 1) \times 64$.
3. **Producer & Mutator Maintenance Rules:**
   - **Literal creation:** Computed statically or at initialization in $O(N)$ once.
   - **`string.append(part)`:**
     - `bytes.push_str(&part.bytes)`
     - `len_codepoints += part.len_codepoints`
     - `is_ascii = is_ascii && part.is_ascii`
     - If non-ASCII and `len_codepoints >= 64`, append new 64-boundary byte offsets using cursor from previous tail.
   - **`string.reserve(n)`:** Calls `bytes.reserve(n)`. Non-semantic capacity hint; zero content modification; metadata unchanged.
   - **`string.char_at(index)`:**
     - Bounds check: $0 \le \text{index} < \text{len\_codepoints}$.
     - If `is_ascii`: byte index is `index`. Returns `bytes.as_bytes()[index] as char`. $O(1)$.
     - If non-ASCII and `index < 64`: start at byte 0, decode `index` code points. At most 63 steps. $O(1)$ bounded.
     - If non-ASCII and `index >= 64`: jump to byte offset `sparse_offsets[(index / 64) - 1]`. Decode `index % 64` code points. At most 63 steps. $O(1)$ bounded.
   - **`string.chars()`:** Returns `Vector[Char]`.
   - **`string.slice(start, length)`:** Derives new `MossString` with freshly validated metadata.
4. **ABI-v10 Bump & Provider Incompatibility:**
   - ABI version is bumped: `ABI_VERSION = 10`.
   - Pre-existing ABI-v9 providers use raw `std::string`. Loading an ABI-v9 `.mossi` into an ABI-v10 compiler fails with `ABI_VERSION_MISMATCH: module was compiled with ABI-v9, required ABI-v10 (String/Char representation changed)`.

---

## Proof 6: Benchmark Methodology and Memory Overhead

### 6.1 Benchmark Integrity Criteria
1. **Release Mode Mandatory:** Native release build (`-O` / `--release`) with LTO.
2. **Runtime-Varying Workloads:** Workloads must be input-dependent to prevent compile-time constant folding.
3. **Four Problem Scales:** 1 KB, 10 KB, 100 KB, 1 MB text inputs.
4. **1 MB Non-ASCII JSON Cursor Parse:** Real-world benchmark exercising `char_at` over mixed ASCII, CJK, and surrogate-pair characters.
5. **Memory Overhead Verification:**
   - ASCII Strings: 0 bytes metadata overhead (vector is empty).
   - Non-ASCII Strings: 4 bytes per 64 code points ($< 6.25\%$ overhead in worst-case UTF-8).

---

**Joint Architecture Sign-off:**  
Team S L6 Coordinator: **APPROVED**  
Team S L6 Independent Architect: **APPROVED**  
Ready for G1, G4, and G3 execution.
