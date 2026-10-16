# Multi-Engine Differential & Adversarial Baseline Report (Phase 25)

**Workstream:** I-D (Multi-Engine Differential & Adversarial Harness)  
**Branch:** `phase25-prep-id`  
**Baseline SHA:** `d714d17c1b1ca34b7765f4853f6d53ccb1df9daf`  
**Tooling Driver:** `tests/phase25/integration/run_differential_adversarial.py`  
**Execution Matrix:** Moss Checking, Fast Debug (checked interpreter), Native Debug, Native Release  

---

## 1. Executive Summary

This report establishes the baseline multi-engine differential and adversarial comparison across all execution engines of the Moss compiler:
1. **Moss Checking:** Front-end parser, semantic analysis, ownership verification, and type checking (`moss check --json`).
2. **Fast Debug:** Checked, bytecode/AST-level Moss interpreter executing reachable Moss source without rustc invocation (`moss run --interp` / `margo debug`).
3. **Native Debug:** Lowering to unoptimized Rust source, debug compilation via `rustc`, and native binary execution (`moss --debug` + `rustc` / `margo run`).
4. **Native Release:** Lowering to release-optimized Rust source, optimized compilation via `rustc -O`, and native binary execution (`moss -O` + `rustc -O` / `margo --release run`).

A comprehensive suite of **29 minimized adversarial fixtures** spanning **10 distinct language and runtime families** was executed against all four engines. The evaluation identified:
- **100% agreement** on valid constructs (e.g. nested vectors, mixed int/float arithmetic, try/recover branches, domain pure initializers, multi-consumer nominal sharing).
- **5 real behavioral disagreements / engine defects**, documented as permanent differential findings rather than normalized away.
- **3 G2-dependent feature categories** correctly identified and isolated as `BLOCKED` pending Team G's G2 Type IR and `key-protocol-v1` delivery.

### Family Evaluation Summary

| Family | Adversarial Family Name | Total Fixtures | Passed | Blocked (G2) | Disagreements Recorded |
|:---:|---|:---:|:---:|:---:|:---:|
| 1 | Generic constructor inference & nested collections | 3 | 3 | 0 | 0 |
| 2 | Empty collection closure & conflicting type constraints | 3 | 2 | 0 | 1 |
| 3 | Method dispatch & unknown fields | 3 | 2 | 0 | 1 |
| 4 | Numeric literals & unary operators | 4 | 2 | 0 | 2 |
| 5 | String/Char edge cases & Unicode | 2 | 2 | 0 | 0 |
| 6 | Map & Set key semantics | 3 | 1 | 2 | 0 |
| 7 | Ownership & consume errors | 3 | 3 | 0 | 0 |
| 8 | Try/recover/except paths | 3 | 3 | 0 | 0 |
| 9 | Imported generic functions & effect summaries | 3 | 3 | 0 | 0 |
| 10 | Cross-package monomorphization & source-free access | 2 | 1 | 1 | 0 |
| **Total** | **All 10 Adversarial Families** | **29** | **22** | **3** | **4** |

---

## 2. Methodology & Comparison Model

### Comparison Dimensions
The differential harness captures and evaluates five orthogonal dimensions across all engines:
1. **Acceptance vs Rejection:** Whether `moss check` accepts the source or rejects it before native lowering. For rejected programs, the harness validates diagnostic code, source line, and teaching cause entities.
2. **Exit Status:** Return code parity across Fast Debug (`0` or interpreter failure code), Native Debug, and Native Release (`0` or runtime failure code).
3. **Standard Output (with Semantic Normalization):**
   - Output comparison is strictly typed and value-checked line by line.
   - For unordered operations (such as Map key/value snapshots), lines are sorted set-wise while strictly checking values.
   - Floating-point text is compared with IEEE numerical equivalence.
4. **Failure Behavior:** When execution fails (e.g. out-of-bounds indexing or assert failure), the harness records whether the engine produced an interpreter diagnostic or native abort/panic signal.
5. **Semantic Query Facts:** Structured verification via `moss type`, `moss effects`, `moss ownership`, and `moss inspect`, ensuring compiler-derived effect summaries and parameter ownership modes (`READ`, `WRITE`, `CONSUME`) match semantic facts.

---

## 3. Real Engine Disagreements & Recorded Defects

The differential harness intentionally exposes engine divergences rather than hiding or normalizing them:

### 1. Unpinned Empty Collection Closure (`RUSTC_E0282`)
- **Fixture:** `family02_empty_closure/02_unpinned_empty_vector_neg`
- **Code:**
  ```moss
  fn main():
    let v = []
  ```
- **Observed Engine Behavior:**
  - **Moss Check:** Accepted without diagnostic (`ok: true`).
  - **Fast Debug:** Executes successfully (exit code `0`).
  - **Native Debug / Release:** Lowering emits `let v = vec![];`. Compilation fails in `rustc` with `error[E0282]: type annotations needed for Vec<_>`.
- **Classification:** Compiler type-closure defect. The checker must require empty collection literals to be pinned by subsequent operations or explicit typed constructor (`Vector[T]()`) before lowering to Rust.

### 2. Unknown Nominal Field Access Hole (`RUSTC_E0609`)
- **Fixture:** `family03_method_dispatch/02_unknown_field_nominal_neg`
- **Code:**
  ```moss
  type Point:
    x: Int

  fn main():
    let p = Point(x: 1)
    echo p.unknown_field
  ```
- **Observed Engine Behavior:**
  - **Moss Check:** Accepted without diagnostic (`ok: true`).
  - **Fast Debug:** Halts at runtime with `interpreter error: unknown field 'unknown_field'` (exit code `1`).
  - **Native Debug / Release:** Compilation fails in `rustc` with `error[E0609]: no field unknown_field on type Point`.
- **Classification:** Checker authority hole. Field access on nominal types must be statically checked and rejected with `UNKNOWN_FIELD`.

### 3. Signed 64-bit Integer Literal Overflow (`RUSTC_OVERFLOW`)
- **Fixture:** `family04_numeric_literals/03_overflow_int_literal_neg`
- **Code:**
  ```moss
  fn main():
    let x = 9223372036854775808
    echo x
  ```
- **Observed Engine Behavior:**
  - **Moss Check:** Accepted without error (`ok: true`).
  - **Fast Debug:** Evaluates to `9223372036854775807` (`i64::MAX`, saturating/clamping).
  - **Native Debug / Release:** Lowering emits `let x = 9223372036854775808_i64;`. Compilation fails in `rustc` with `error: literal out of range for i64` (`#[deny(overflowing_literals)]`).
- **Classification:** Lexer and checker integer boundary validation gap. Positive integer literals exceeding `2^63 - 1` must be rejected at compile time.

### 4. Unary Negation Overflow of `INT64_MIN` (`RUSTC_OVERFLOW`)
- **Fixture:** `family04_numeric_literals/04_unary_neg_overflow_neg`
- **Code:**
  ```moss
  fn main():
    let min_int = -9223372036854775808
    let neg = -min_int
    echo neg
  ```
- **Observed Engine Behavior:**
  - **Moss Check:** Accepted (`ok: true`).
  - **Fast Debug:** Two's-complement wrap-around produces `-9223372036854775808`.
  - **Native Debug / Release:** Lowering emits `let neg = -min_int;`. Compilation fails in `rustc` with `error: this arithmetic operation will overflow: attempt to negate i64::MIN, which would overflow`.
- **Classification:** Arithmetic evaluation disparity between interpreter and Rust constant propagation.

### 5. Runtime Failure Signal Divergence
- **Fixture:** `family05_string_unicode/02_string_oob_char_at_neg`
- **Code:**
  ```moss
  fn main():
    let s = "short"
    echo s.char_at(10)
  ```
- **Observed Engine Behavior:**
  - **Fast Debug:** Exits cleanly with returncode `1` and structured error message `String character index out of bounds`.
  - **Native Debug / Release:** Aborts with exit code `134` / `-6` (`SIGABRT` due to Rust panic).
- **Classification:** Expected execution engine divergence between interpreted managed execution and native unhandled panic.

---

## 4. G2-Dependent Capabilities (Marked BLOCKED)

Three fixtures evaluate language features that are architecturally dependent on Team G's G2 Type IR and key-protocol-v1 runtime:

### 1. Float Map Keys (`06_02_float_keys_map_blocked`)
- **Language Contract:** Float keys require distinct signed zero identities (`-0.0 != +0.0`) and single NaN identity under Julia-like `isless` total order.
- **Current Baseline Status:** `moss check` and Fast Debug accept `m[0.0] = "zero"`. Native lowering emits `HashMap<f64, String>`, which rustc rejects with `error[E0277]: the trait bound {float}: Eq is not satisfied`.
- **Resolution:** G2 provides `MossMap<K, V>` and `KeyProof` adapters avoiding Rust orphan rules.

### 2. Custom Nominal Map Keys (`06_03_custom_nominal_keys_blocked`)
- **Language Contract:** Types defining structural `isequal(other: Self) -> Bool` and `hash() -> Int` can be Map keys.
- **Current Baseline Status:** Fast Debug resolves dispatch through object methods, while native lowering fails in rustc with missing `Eq` and `Hash` traits on the generated struct.
- **Resolution:** Blocked on G2 provider-emitted `KeyProof` adapter lowering.

### 3. Source-Free Cross-Package Monomorphization (`10_02_source_free_monomorph_blocked`)
- **Language Contract:** Compiled-only packages without source `.moss` files provide `.mossi` interfaces for consumer monomorphization.
- **Current Baseline Status:** Fast Debug rejects source-free providers with `FAST_DEBUG_NATIVE_DEPENDENCY` before execution. Native compilation requires ABI-v10 Type IR metadata.
- **Resolution:** Blocked on G2 Type IR and ABI-v10 release.

---

## 5. Detailed Fixture Results Ledger

| Family | Fixture Name | Layout | Expectation | Check | Fast Debug | Native Debug | Status |
|:---:|---|---|---|:---:|:---:|:---:|:---:|
| 1 | `01_nested_vectors_pos` | standalone | `pass` | PASS | rc=0 | rc=0 | **PASS** |
| 1 | `02_map_of_vectors_pos` | standalone | `pass` | PASS | rc=0 | rc=0 | **PASS** |
| 1 | `03_nested_mismatch_neg` | standalone | `reject TYPE_INFERENCE_FAILED` | TYPE_INFERENCE_FAILED | N/A | N/A | **PASS** |
| 2 | `01_empty_vector_pinned_pos` | standalone | `pass` | PASS | rc=0 | rc=0 | **PASS** |
| 2 | `02_unpinned_empty_vector_neg` | standalone | `disagreement RUSTC_E0282` | PASS | rc=0 | rc=1 | **PASS** |
| 2 | `03_conflicting_branch_closure_neg` | standalone | `reject MOSS_COMPILE_ERROR` | MOSS_COMPILE_ERROR | N/A | N/A | **PASS** |
| 3 | `01_nominal_field_access_pos` | standalone | `pass` | PASS | rc=0 | rc=0 | **PASS** |
| 3 | `02_unknown_field_nominal_neg` | standalone | `disagreement RUSTC_E0609` | PASS | rc=1 | rc=1 | **PASS** |
| 3 | `03_untyped_receiver_method_neg` | standalone | `reject MOSS_COMPILE_ERROR` | MOSS_COMPILE_ERROR | N/A | N/A | **PASS** |
| 4 | `01_mixed_int_float_pos` | standalone | `pass` | PASS | rc=0 | rc=0 | **PASS** |
| 4 | `02_int64_min_literal_pos` | standalone | `pass` | PASS | rc=0 | rc=0 | **PASS** |
| 4 | `03_overflow_int_literal_neg` | standalone | `disagreement RUSTC_OVERFLOW` | PASS | rc=0 | rc=1 | **PASS** |
| 4 | `04_unary_neg_overflow_neg` | standalone | `disagreement RUSTC_OVERFLOW` | PASS | rc=0 | rc=1 | **PASS** |
| 5 | `01_unicode_codepoint_pos` | standalone | `pass` | PASS | rc=0 | rc=0 | **PASS** |
| 5 | `02_string_oob_char_at_neg` | standalone | `failure RUNTIME_BOUNDS_ERROR` | PASS | rc=1 | rc=-6 | **PASS** |
| 6 | `01_scalar_map_keys_pos` | standalone | `pass` | PASS | rc=0 | rc=0 | **PASS** |
| 6 | `02_float_keys_map_blocked` | standalone | `pass` | PASS | N/A | N/A | **BLOCKED** |
| 6 | `03_custom_nominal_keys_blocked` | standalone | `pass` | PASS | N/A | N/A | **BLOCKED** |
| 7 | `01_reinit_consumed_pos` | standalone | `pass` | PASS | rc=0 | rc=0 | **PASS** |
| 7 | `02_write_read_conflict_neg` | standalone | `reject OWNERSHIP_CONFLICTING_ACCESS` | OWNERSHIP_CONFLICTING_ACCESS | N/A | N/A | **PASS** |
| 7 | `03_use_after_consume_neg` | standalone | `reject OWNERSHIP_USE_AFTER_CONSUME` | OWNERSHIP_USE_AFTER_CONSUME | N/A | N/A | **PASS** |
| 8 | `01_try_recover_handled_pos` | standalone | `pass` | PASS | rc=0 | rc=0 | **PASS** |
| 8 | `02_recovery_join_type_mismatch_neg` | standalone | `reject MOSS_COMPILE_ERROR` | MOSS_COMPILE_ERROR | N/A | N/A | **PASS** |
| 8 | `03_unhandled_raise_neg` | standalone | `reject MOSS_COMPILE_ERROR` | MOSS_COMPILE_ERROR | N/A | N/A | **PASS** |
| 9 | `01_pure_domain_init_pos` | standalone | `pass` | PASS | rc=0 | rc=0 | **PASS** |
| 9 | `02_impure_domain_init_neg` | standalone | `reject MOSS_COMPILE_ERROR` | MOSS_COMPILE_ERROR | N/A | N/A | **PASS** |
| 9 | `03_imported_helper_pos` | packages | `pass` | PASS | rc=0 | rc=0 | **PASS** |
| 10 | `01_multiple_consumers_type_identity_pos` | packages | `pass` | PASS | rc=0 | rc=0 | **PASS** |
| 10 | `02_source_free_monomorph_blocked` | packages | `pass` | PASS | N/A | N/A | **BLOCKED** |

---

## 6. Recommendations for Workstream Integration

1. **Preflight Gating in CI:** Integrate `run_differential_adversarial.py --self-test` and `run_differential_adversarial.py --all` into Phase 25 integration verification.
2. **Checker Authority Hardening:** Prioritize closing the checker authority holes for:
   - Unpinned empty collections (force explicit annotation or require pinning).
   - Unknown nominal fields (reject at checker before native code generation).
   - Out-of-bounds integer literals.
3. **G2 Unblocking Track:** Once Team G's G2 Type IR and `key-protocol-v1` are merged, re-run `run_differential_adversarial.py --family 6` and `--family 10` to unblock and activate the remaining three test cases.
