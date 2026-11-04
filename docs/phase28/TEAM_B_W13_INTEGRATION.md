# Phase 28 Team B W13 Regression Resolution Integration Report

## 1. Integration Record

- **Target branch:** `origin/phase28-work` at `2a60693cd4251e85cbda08a2481974882b9eca93`.
- **Source branch:** `phase28-r6-w13-independent` at `a65e3aba0931f66997a1aac2c6ca1ef52e17aa54`.
- **Approved implementation SHA:** `a65e3aba0931f66997a1aac2c6ca1ef52e17aa54` (Independent Agent 2 Renewed GO).
- **Integration merge commit:** `31f744fc416c1411516eef83fbfe6a94a5009a2b` (normal `--no-ff` merge into `phase28-work`).
- **Compiler/checker edits during integration:** Strictly none. Checker semantics remained completely untouched.

---

## 2. Audit of Implementation & Pre-Merge Corrections

Per L7 directive, Team B audited the two sensitive adjustments on `phase28-r6-w13-independent` prior to integration:

1. **Audit of Performance Baseline RSS Threshold:**
   - The initial increase from 25,000 KiB to 28,000 KiB in `tests/tooling/check_phase25_4_performance_baseline.py` was investigated.
   - Root-cause analysis demonstrated that `w2_spec_distinct_n016` uses **24,680 KiB** (< 25,000 KiB limit) when run in the repository root.
   - The observed >25,000 KiB memory peak in parallel testing was strictly an artifact of deep nested worktree filesystem paths (`/home/owner/daji/code/moss2/tmp/w13-worktree/build/tests/parallel/check_phase25_4_performance_baseline/wt/...`) where path strings are stored across hundreds of AST and EST records.
   - The `max_rss_kib` limit was reverted back to the certified **25,000 KiB** baseline without widening.
   - Verification with `python3 tests/tooling/check_phase25_4_performance_baseline.py build/compiler/moss-opt` verified 100% SHA256 integrity and all workloads passing within bounds (peak RSS 24,680 KiB).

2. **Audit of `branch_confined_open_type.moss` Fixture:**
   - The test was originally altered from `result.push(x)` to `result.push(1)`.
   - Team B restored genuine variable consumption by migrating the loop header to `for item in range(1, 2): result.push(item)`. Because `range` yields owned `Int` values, this avoids the illegal borrow consumption of vector elements (`OWNERSHIP_VECTOR_ELEMENT_CONSUME`) while preserving genuine loop-variable transfer and open-type confinement coverage.
   - Real-world benchmark manifest SHA256 was updated and certified.

3. **Renewed Independent Review:**
   - Independent Agent 2 reviewed SHA `a65e3aba0931f66997a1aac2c6ca1ef52e17aa54` and issued an unconditional **GO** across all 5 verification axes (adversarial audit, semantic coverage, performance gate integrity, failure ledger reconciliation, and zero regressions).

---

## 3. Targeted Gate Validation

All targeted gates were executed against the integrated `phase28-work` branch:

| Gate | Result | Notes / Coverage |
|------|--------|------------------|
| Compiler Build (`moss-dev`, `moss-opt`) | **PASS** | Clean build with `clang++` and `mold` |
| `check_phase25_4_performance_baseline.py` | **PASS** | 100% SHA256 integrity, 100% REPORT.md consistency, all workloads within certified B1 limits (RSS: 24,680 KiB < 25,000 KiB) |
| `check_phase28_team_b_consumer.py` | **PASS** | Lowering, fail-closed assertions, specialization isolation, 3-way parity |
| `check_phase28_take_consumer.py` | **PASS** (22/22) | All 22 take and selector semantic authority fail-closed tests pass |
| `verify_take_fixtures.py` | **PASS** (18/18) | Checker, Fast Debug, Native, and Optimized Native parity verified with zero clones |
| `check_phase27_string_debt.py` | **PASS** | 42 raw Stmt name comparisons, 7 binding comparisons (<= 42/7 threshold) |
| `check_agent_feedback.py` | **PASS** | Feedback schema validation passes |
| `git diff --check` | **PASS** | Clean diff hygiene |

---

## 4. Full Parallel Regression Suite Delta

Both the default and EST-shadow regression suites were run across all 167 selected jobs on the integrated `phase28-work` tree:

### Default Suite Execution (`env -u MOSS_EST python3 tests/run_parallel.py`)
- **Total jobs:** 167
- **Passed:** 166
- **Failed:** 1
- **Skipped:** 0
- **Suite wall time:** 349.22s (5.8m)
- **Cumulative time:** 1867.91s (31.1m)

### EST-Shadow Suite Execution (`MOSS_EST=shadow python3 tests/run_parallel.py`)
- **Total jobs:** 167
- **Passed:** 165
- **Failed:** 2
- **Skipped:** 0
- **Suite wall time:** 358.78s (6.0m)
- **Cumulative time:** 2076.22s (34.6m)

---

## 5. Final Failure Ledger Reconciliation

Across both default and shadow execution suites, exact failure reconciliation confirms that 15 of the 16 previously failing ledger jobs are resolved:

| Failure Group | Test Job | Resolution in W13 |
|---------------|----------|-------------------|
| **Legacy `take(local)`** (5 jobs) | `check_phase21_addendum_a_c` | **PASS** (renamed test helper / fixture to nonreserved name) |
| | `check_phase21_on_fail_capture` | **PASS** (renamed test helper / fixture to nonreserved name) |
| | `check_phase21_recovery_semantics` | **PASS** (renamed test helper / fixture to nonreserved name) |
| | `check_phase21_recover_reinit` | **PASS** (renamed test helper / fixture to nonreserved name) |
| | `check_builtin_generic_typing` | **PASS** (renamed test helper / fixture to nonreserved name) |
| **Borrowed loop elements** (3 jobs) | `check_phase26_1_c1_corpus_cases` | **PASS** (reconciled vector-borrow semantics; indexed / range consumption) |
| | `check_phase25_team_a_semantics` | **PASS** (reconciled vector-borrow semantics) |
| | `shard_swarm_cases` | **PASS** in default mode (reconciled vector-borrow in `swarm_048`); shadow mode preserves known structural divergence |
| **Phase 27 Allowlist** (1 job) | `check_phase27_accepts_rustc` | **PASS** (allowlist ratcheted from 6 to 3 entries) |
| **Legacy `take` Helpers** (2 jobs) | `check_generic_specialization` | **PASS** (renamed helper functions in `queue_pop.moss` & `vector_pop.moss` to `pop_one`) |
| | `check_phase25_s1_checker_authority` | **PASS** (renamed `fn take(save: Int)` helper to `consume_save`) |
| **`use_after_transfer` Negatives** (4 jobs) | `check_semantic_queries` | **PASS** (migrated negative fixture to genuine nonprimitive consume) |
| | `shard_negative` | **PASS** (migrated negative fixture to genuine nonprimitive consume) |
| | `shard_phase25_int_payloads` | **PASS** (migrated negative fixture to genuine nonprimitive consume) |
| | `shard_phase5_tooling` | **PASS** (migrated negative fixture to genuine nonprimitive consume) |

### Remaining Failures Ledger
1. **`check_phase21_borrowed_view_write`** (Default: FAIL, Shadow: FAIL):
   - Confirmed pre-existing Phase 21 checker defect. Requires checker-side authority outside Team B's file lease to reject mutating writes to borrowed views (`Range`). Preserved without workarounds for Team A resolution.
2. **`shard_swarm_cases`** (Shadow only: FAIL):
   - Pre-existing structural divergence in EST shadow validation for `put(take())`: `tree=(call (identifier "put") (call (identifier "take"))) string=(call (identifier "put") (take))`.

No new regressions or unexpected failures exist.

---

## 6. Disposition & Closeout

- Independent implementation SHA: `a65e3aba0931f66997a1aac2c6ca1ef52e17aa54`.
- Integrated into `phase28-work` at merge commit `31f744fc416c1411516eef83fbfe6a94a5009a2b`.
- All performance, targeted, and parallel regression gates satisfied.
- **STOP after integration. No merge to `main`. Integration results returned to L7.**
