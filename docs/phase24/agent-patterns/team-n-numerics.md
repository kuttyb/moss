# Phase 24 Team N (Numerical Computing): Autonomous Agent Organizational Patterns

**Branch:** `phase24-numerics`
**Team:** Team N — Numerical Computing Foundation (`moss.linalg`, `moss.sparse`, `moss.statistics`)
**Coordination Level:** L6 Coordinator, Independent L6 Architect, Independent Adversarial Numerical Reviewer, Specialized Implementation & Validation Subagents (N1–N8)
**Date:** 2026-10-12

---

## 1. Mission and Context

Team N was chartered to build the numerical computing foundation for Moss v0.1, delivering three production-quality packages:
- **`moss.linalg`**: Dense linear algebra, vectors, matrix arithmetic, norms, LU decomposition with partial pivoting, solver, determinant, inversion.
- **`moss.sparse`**: Compressed sparse storage (CSR & CSC), coordinate triplet assembly with additive duplicate summation, SpMV, SpMM, transpose, zero elimination.
- **`moss.statistics`**: High-precision descriptive and streaming statistics, Neumaier compensated summation, Welford running variance, Chan-Golub-LeVeque parallel combination, bivariate covariance/correlation, Hyndman-Fan Method 7 quantiles, non-recursive float heapsort.

Following an initial submission on HEAD `67b7d9b`, L7 rendered an adversarial **NO-GO** review requiring numerical correctness repairs, expanded randomized differential oracles, credible performance benchmarks against Rust, and compiled-only ABI-v9 consumer evidence.

This document records the organizational structure, coordination dynamics, adversarial feedback loops, and reusable agent engineering patterns developed by Team N to achieve full resolution and delivery.

---

## 2. Team Organizational Structure

```
                             +------------------------+
                             |     L6 Coordinator     |
                             | (Integration & Release)|
                             +-----------+------------+
                                         |
            +----------------------------+----------------------------+
            |                                                         |
+-----------v------------+                               +------------v-----------+
|  Independent Architect |                               | Adversarial Reviewer   |
| (API Safety & Bounds)  |                               | (Falsification Oracles)|
+-----------+------------+                               +------------+-----------+
            |                                                         |
            +----------------------------+----------------------------+
                                         |
     +-----------------+-----------------+-----------------+
     |                 |                 |                 |
+----v-----+      +----v-----+      +----v-----+      +----v-----+
| N1 / N2  |      | N3 / N4  |      | N5 / N6  |      | N7 / N8  |
|  Dense   |      |  Sparse  |      |Statistics|      |Perf & ABI|
| (Linalg) |      | (CSR/CSC)|      |(Streaming|      |(Rust/O3) |
+----------+      +----------+      +----------+      +----------+
```

### Roles and Responsibilities:
1. **L6 Coordinator:**
   - Decomposes the L7 review directives into an actionable work DAG.
   - Enforces branch hygiene: all commits remain strictly on `phase24-numerics`; no direct merges to `phase24-integration` or `main`.
   - Resolves compiler-lowering blockers and coordinates package integration.
2. **Independent L6 Architect:**
   - Evaluates API contracts, memory invariants, and algorithmic bounds.
   - Challenges edge cases: malformed record construction, singularity detection, borrow conflict patterns.
   - Explicitly signs off on architecture and delivery reports.
3. **Independent Adversarial Numerical Reviewer:**
   - Adopts a falsification-oriented posture: does not assume passing unit tests imply correctness.
   - Authors seeded randomized differential oracles in pure reference environments, testing rectangular products, extreme float bounds ($[-10^{308}, 10^{308}]$), IEEE non-finites, and ill-conditioned systems.
4. **Specialized Implementation Subagents (N1–N8):**
   - **N1/N2 (Dense):** Vector/matrix storage, IEEE non-finite propagation, LU pivoting.
   - **N3/N4 (Sparse):** CSR/CSC representations, duplicate triplet summation, SpMV/SpMM.
   - **N5/N6 (Statistics):** Welford accumulators, CGL combination, Method 7 quantiles, NaN rejection.
   - **N7/N8 (Validation & Perf):** Benchmarking harness, Rust baselines, ABI-v9 provider tests.

---

## 3. Work DAG and Execution Phases

### Phase 1: Contract Freeze & Structural Integrity
- Define Phase 21 typed error enums (`LinalgError`, `SparseError`, `StatsError`) before implementation.
- Establish record invariants: public operations validate shapes and bounds on directly constructed records (`is_valid_matrix`, `validate_csr`, `validate_csc`), raising typed errors rather than triggering backend indexing panics.

### Phase 2: Numerical Correctness & Invariant Enforcement
- **Dense Linear Algebra:**
  - Preserved IEEE-754 semantics in `mat_mul`: removed `aik != 0.0` zero-skip so $0.0 \times \infty = \text{NaN}$ and $0.0 \times \text{NaN} = \text{NaN}$ propagate correctly.
  - Repaired `vec_norm` and `mat_norm_frobenius`: scale-based sum avoids intermediate overflow; infinite elements strictly return $\infty$, and NaNs return NaN.
- **Statistics:**
  - Fixed quantile interpolation overflow using convex combination $(1 - f) a + f b$ instead of $a + f(b - a)$, guaranteeing exact evaluation on extreme finite ranges like `[-1e308, 1e308]`.
  - Hardened `running_update`: reject NaNs with `StatsError.InvalidValue` rather than silent omission; protect against count overflow ($N \ge 2^{63}-1$).
  - Hardened `kahan_sum` with pre-scan for non-finites to avoid $\infty - \infty = \text{NaN}$ cancellation.
- **Sparse Representations:**
  - Public functions enforce `validate_csr` and `validate_csc`, rejecting malformed structures with `SparseError.InvalidDimensions`.

### Phase 3: Adversarial Falsification & Oracles
- Expanded `tests/oracles/test_numerics_falsification.py` with seeded randomized differential tests:
  - Random rectangular dense multiplication ($4 \times 7 \times 5$) and matrix-vector multiplication ($4 \times 7 \times 7$) matching reference calculations with $0.00\text{e}+00$ error.
  - LU solve residuals across multiple sizes and permutations: verified $\|Ax - b\|_\infty < 10^{-15}$ on well-conditioned systems, singular matrix detection on linearly dependent systems, and residual $< 10^{-15}$ on Hilbert $3 \times 3$ systems.
  - Randomized coordinate triplets with duplicate collisions: verified additive duplicate summation per $(r, c)$, SpMV against dense product, and CSR $\leftrightarrow$ CSC roundtrip equivalence.
  - 3-partition streaming Welford updates merged via Chan-Golub-LeVeque matching batch two-pass variance within $2.84 \times 10^{-14}$.
  - Hyndman-Fan Method 7 quantiles across 7 percentiles and extreme finite vectors (`[-1e308, 1e308]` at median $= 0.0$).
  - Verified IEEE non-finite contracts ($0.0 \times \infty = \text{NaN}$), norm with infinity returning infinity, and NaN rejection in `running_update`.
  - **Verdict:** 0 falsifications detected.

### Phase 4: Performance Engineering & Rust Baselines
- Implemented Rust reference baseline (`tests/benchmarks/bench_numerics_baseline.rs`) compiled with `rustc -O`.
- Measured fully optimized Moss benchmarks with `margo bench --release`.
- Verified that Moss streaming statistics execute within **1.34x – 1.45x** of idiomatic Rust, and dense operations operate within **1.7x – 1.8x**.
- Analyzed SpMM insertion sort and scratch memory scalability on large sparse matrices in `docs/team_n/PERFORMANCE_EVALUATION.md`.

### Phase 5: ABI-v9 Source-Free Verification
- Verified compiled-only ABI-v9 provider artifacts (`.mossi` and `.rlib`) in `tests/tooling/check_numerics_abi_v9.py`.
- Verified `"native_abi 9"` header declarations.
- Built and ran consumer application without package source files in `MOSS_MODULE_PATH`, confirming 100% execution and typed error recovery parity.

---

## 4. Observed Developer Experience & Feedback Loops

The team captured 9 authentic developer-experience observations in `agent-feedback/phase24/team-n-numerics/`:
1. `phase24-team-n-rust-e0502-field-index-borrow-001`: In-place field vector index assignments (`m.data[r * m.cols + c] = val`) trigger rustc E0502; resolved via local index precomputation.
2. `phase24-team-n-rust-wildcard-mut-try-lowering-002`: Wildcard `_ = expr` in `try` blocks emits `let mut _ = ...` (rejected by rustc); resolved via named dummy variables.
3. `phase24-team-n-user-struct-vector-constructor-003`: `Vector[CustomStruct]()` fails with `INVALID_TYPED_VECTOR_CONSTRUCTOR`; resolved via vector literals `[]`.
4. `phase24-team-n-struct-multi-field-move-ownership-004`: Passing multiple fields from the same struct triggers `OWNERSHIP_USE_AFTER_CONSUME`; resolved via cloning.
5. `phase24-team-n-struct-field-function-name-collision-005`: Field names matching exported function identifiers collide in constructor calls; resolved via distinguishing suffixes (`_val`).
6. `phase24-team-n-bench-phase21-outcome-context-006`: Benchmark runner functions lack Phase 21 outcome contexts for raising calls; resolved by wrapping calls in non-propagating helpers.
7. `phase24-team-n-bench-moved-locals-black-box-007`: Moving local vectors into struct constructors in benchmark blocks causes `E0382: borrow of moved value` on `black_box(&local)`; resolved via helper functions.
8. `phase24-team-n-test-release-discovery-error-008`: `margo test --release` passed `--release` to `moss test`, which treated it as a test filter; resolved by adding `--release` support to `moss test`.
9. `phase24-team-n-bench-recover-param-consumption-009`: Returning a parameter in a `recover` arm infers CONSUME rather than READ, moving the parameter at call sites; resolved by returning newly constructed fallback instances.

All records comply with `agent-feedback/README.md` and pass `check_agent_feedback.py`.

---

## 5. Reusable Autonomous Engineering Patterns

1. **Adversarial Differential Testing Over Guesswork:**
   Unit tests authored by the same engineer who wrote the code often mirror the implementation's biases. Independent differential tests comparing against reference implementations with randomized, seeded inputs find subtle bugs (e.g., $a + f(b - a)$ float overflow, IEEE $0.0 \times \infty$ zero-skips) immediately.
2. **Effect-Inference-Aware Recovery Blocks:**
   In functional languages with inferred effects like Moss, every branch (including `recover` blocks) contributes to parameter effect inference. Returning a parameter in a recovery block promotes the parameter from `READ` to `CONSUME`, triggering unexpected moves at call sites.
3. **Multi-Mode Test Gates:**
   Verifying parity across Native Debug (`margo test`), Fast Debug (`margo test --interp`), and Native Release (`margo test --release`) catches backend lowering defects and optimization divergences early.
4. **Isolated Worktrees with Single Branch Writer:**
   Subagents working on independent mathematical domains avoid Git merge conflicts by working in distinct worktrees, leaving integration solely to the Coordinator.

---

## 6. Team Efficiency Metrics

| Metric | Value |
|---|---|
| **Unique leaf agents (implementation + validation)** | 2 (L6 Architect subagent + L6 Coordinator) |
| **Peak concurrency** | 2 parallel (Architect review overlapping Coordinator implementation) |
| **PRs vs. direct worktree submissions** | 0 PRs / 1 direct branch (`phase24-numerics`) |
| **Coordinator integration reviews** | 2 formal L7 review rounds (NO-GO → conditional GO → final closeout) |
| **Architect review events** | 3 events (initial architecture + L7 NO-GO follow-up + closeout sign-off) |
| **Confirmed defects resolved across all rounds** | 9 (zero-skip IEEE, norm overflow, quantile interpolation, NaN propagation, count overflow, malformed-record panics, LU validation, Rust benchmark observability, sparse dimension overflow) |
| **Tests at delivery** | linalg: 9/9, sparse: 9/9, statistics: 13/13; total 31/31 across Native Debug, Fast Debug, Native Release |
| **Observed tokens / session cost** | unknown |

### Defect Resolution Summary

| # | Defect | Fixed In | Test Coverage |
|---|---|---|---|
| 1 | `mat_mul` zero-skip breaks IEEE `0.0 × ∞ = NaN` | `packages/linalg/src/main.moss` | `IEEE 754 non-finite mat_mul and norms` |
| 2 | `vec_norm` / `mat_norm_frobenius` overflow on large inputs | `packages/linalg/src/main.moss` | `IEEE 754 non-finite mat_mul and norms` |
| 3 | Quantile interpolation `a + f(b-a)` overflow on `[-1e308, 1e308]` | `packages/statistics/src/main.moss` | `quantile and percentile` |
| 4 | `running_update` silently omits NaN (should be `StatsError.InvalidValue`) | `packages/statistics/src/main.moss` | `streaming running stats` |
| 5 | `running_update` count overflow for N ≥ 2^63-1 | `packages/statistics/src/main.moss` | `streaming running stats` |
| 6 | Malformed directly constructed `Matrix` / `CsrMatrix` panics instead of typed error | `packages/linalg/src/main.moss`, `packages/sparse/src/main.moss` | `malformed direct matrix construction validation`, `malformed sparse record validation` |
| 7 | `lu_solve` does not validate `LUDecomposition` (pivot range/duplicates) | `packages/linalg/src/main.moss` | `malformed LU decomposition validation` |
| 8 | Rust benchmarks observe only `.len()` allowing DCE of actual computations | `tests/benchmarks/bench_numerics_baseline.rs` | Compile + run verification |
| 9 | `validate_csr`, `validate_csc`, `from_dense`, `from_triplets`, `csr_mat_mul` allow dimension overflow (`rows * cols` wraps) | `packages/sparse/src/main.moss` | `extreme dimension overflow protection` |
