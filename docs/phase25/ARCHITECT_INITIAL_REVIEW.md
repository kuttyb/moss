# Independent L6 Architect Review: Phase 25 G0 Architecture & G1 Defect Ledger

**Reviewer:** Independent L6 Architect
**Review Target:** `docs/phase25/G0_JOINT_ARCHITECTURE_PROOFS.md`, `docs/phase25/TEAM_S_DESIGN_AND_ACCEPTANCE.md`
**Target Commit SHA:** `fa3a340a0a6652568b97869489da1a84038b9857`
**Verdict:** **CONDITIONAL GO FOR G1 & G4 IMPLEMENTATION** (Subject to Falsification Regressions for G1 Ledger)

---

## 1. Evaluation of G0 Architecture Proofs

### Proof 1: Provider-Owned Generics & Consumer Free-Functions
- **Evaluation:** SOUND. Lowering methods on external provider types to standalone free functions in consumer crates (`moss__fn__sum(this: &Point)`) avoids Rust orphan rules (`E0117`) because no orphan `impl Trait for ForeignType` or `impl ForeignType` is generated.
- **Architect Challenge:** What if two consumer crates define a method with the same name on the same provider type, and an application imports both?
  - *Resolution:* In Moss, modules are strictly qualified on import, and method calls resolve statically at compile time based on imported scope. The compiler must ensure that if a method name is ambiguous between two imported modules, `METHOD_AMBIGUOUS` is raised rather than picking one arbitrarily.
- **Scope Limit Verified:** Confirmed this applies strictly to ordinary value methods, and excludes domain handlers and FileIO.

### Proof 2: Static Map/Set Key Dispatch & Float Julia `isless`
- **Evaluation:** SOUND & RIGOROUS. The `MossKey_T` transparent wrapper strategy correctly places the `PartialEq`, `Eq`, and `Hash` implementations in crate $P$ where type $T$ is defined, completely preventing orphan rule violations.
- **Architect Challenge on Float Ordering:**
  - Rust's `f64::total_cmp` places `-NaN < -inf < ... < +NaN`.
  - Moss specification requires Julia `isless`: `finite < NaN`, `-0.0 < +0.0`, and `isequal(NaN, NaN) == true`.
  - *Verdict:* The mathematical proof in §2.2 is valid. The `moss_float_less` implementation correctly partitions $[-\infty, -0.0] < [+0.0, +\infty] < \{\text{NaN}\}$.
  - *Requirement:* Permanent property tests must verify all 8 mathematical laws (irreflexivity, asymmetry, transitivity, totality, reflexive equality, symmetric equality, transitive equality, hash consistency) against diverse bit-pattern NaNs (positive quiet NaN, negative quiet NaN, signaling NaN payload), positive zero, negative zero, subnormals, and infinities.

### Proof 3: Target Capability Preflight
- **Evaluation:** SOUND. Preflight verification must run after semantic check and before execution startup. If `moss run --interp` detects `FileIO`, it must reject immediately with `FAST_DEBUG_FILEIO` before any statement executes.

### Proof 4: Differential CI
- **Evaluation:** SOUND. Unordered map serialization normalization is essential because hash seeds and internal table bucket iterations are unspecified. Strict 64-bit precision preservation without double approximation is verified.

### Proof 5: Char Representation and String Metadata (ABI-v10)
- **Evaluation:** SOUND. Eager code-point count, ASCII flag, and sparse 64-code-point byte offsets bound `char_at(index)` to at most 63 linear step operations ($O(1)$ bounded time).
- **Architect Challenge on Slicing & Appending:**
  - When `string.append` is called repeatedly in a loop, sparse offsets must be amortized. Re-scanning the whole string would make append $O(N^2)$.
  - *Requirement:* `append` must resume scanning from the previous string end cursor when computing new 64-boundary offsets.
  - *Requirement:* ABI-v10 migration must fail fast with `ABI_VERSION_MISMATCH` if any ABI-v9 provider is loaded.

---

## 2. Independent Audit & Falsification of G1 Defect Ledger

The Architect independently inspected the codebase to verify root causes for the assigned G1 defects:

### Defect #1: Fast Debug 64-bit Integer Precision Loss
- **Source Inspection:** `src/interpreter.hpp:1303-1304`:
  ```cpp
  double l = left.kind == Value::Kind::Int ? left.integer : left.floating;
  double r = right.kind == Value::Kind::Int ? right.integer : right.floating;
  ```
- **Confirmation:** CONFIRMED DEFECT. Comparing two `Int`s converts them to `double`. An integer $> 2^{53}$ (e.g. $9007199254740993$ vs $9007199254740992$) loses precision and falsely reports equality or wrong comparison!
- **Requirement:** Fast Debug must compare `int64_t` directly when both operands are `Int`.

### Defect #4: Nested Index Assignment Borrow Conflict
- **Source Inspection:** `src/moss.cpp:20999`:
  `m.data[r * m.cols + c] = v` lowers directly to `(m.data)[(r * m.cols + c) as usize] = v;`.
- **Confirmation:** CONFIRMED DEFECT. In Rust, borrowing `m.data` as mutable while evaluating `m.cols` immutably in the index expression triggers Rust borrow checker error: `cannot borrow m as immutable because it is also borrowed as mutable`.
- **Requirement:** The index expression must be evaluated into a temporary `let __moss_idx = ...;` prior to the indexed assignment.

### Defect #5: `_ = f()` Discard Lowering
- **Source Inspection:** `src/moss.cpp:21012`: Emits `let mut _ = ...;`.
- **Confirmation:** CONFIRMED DEFECT. In Rust, `let mut _` is illegal syntax (`cannot be marked as mut`). Furthermore, repeated `_ = f()` cannot reassign `_` without `let`.
- **Requirement:** Emit `let _ = ...;` in Rust native lowering, and handle discard cleanly in Fast Debug without crashing.

### Defect #6: Assert Diagnostics with `{` or `}`
- **Source Inspection:** `src/moss.cpp:21184-21189`:
  Emits `panic!("... " + s.text + "; actual={:?}, expected={:?}", actual, expected);`.
- **Confirmation:** CONFIRMED DEFECT. If `s.text` contains `{` or `}` (e.g. dictionary or set literal), Rust's `panic!` interprets it as format string tokens and fails with invalid format string.
- **Requirement:** Escape `{` and `}` in assertion diagnostic format strings.

### Defect #8: Fast Debug `for` Loop Support
- **Source Inspection:** `src/interpreter.hpp:1834`:
  `case Stmt::Kind::For: throw RuntimeError(statement.line, "Fast Debug does not support for iteration yet");`
- **Confirmation:** CONFIRMED DEFECT. Fast Debug unconditionally throws at runtime on `for` loops.
- **Requirement:** Implement full `for` traversal over `range`, `Vector`, and `Queue` in Fast Debug interpreter.

---

## 3. Verdict & Next Steps

**VERDICT: CONDITIONAL GO.**
G0 architecture proofs are approved. G1 defect analysis is verified against root causes. Implementation may proceed on:
1. G1 parity fixes (Fast Debug exact Int, for loops, borrow lowering, assert escaping, discard lowering).
2. G4 String metadata and distinct Char implementation.
3. Regression test harness and differential CI.
