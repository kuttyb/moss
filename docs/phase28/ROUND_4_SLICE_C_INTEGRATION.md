# Phase 28 Round 4 Slice C Integration Report

## 1. Integration Record

- **Target branch:** `origin/phase28-work` at `ea7208964b29edb3363aa5cd58acf6d5321af833`.
- **Source branch:** `origin/phase28-r4-slice-c` at `10fb7361560418f36a321b15db4987835ac1fe53`.
- **Approved implementation SHA:** `78d63cb2287d6a4b81def3ae35a73356bc0d3f3b` (Agent 2 GO).
- **Source/test integrity verification:** `git diff 78d63cb2287d6a4b81def3ae35a73356bc0d3f3b..10fb7361560418f36a321b15db4987835ac1fe53 -- src/ tests/ sdk/` is completely empty (zero source, test, or SDK differences). The approved implementation is preserved exactly.
- **Integration merge commit:** `528c4e46882a967100cebe4a363f2e321d0de94f` (normal `--no-ff` merge into `phase28-work`).
- **Compiler/test edits during integration:** Strictly none. No compiler source or test expectations were modified.

---

## 2. SDK Anomaly Baseline Verification

Before merging, `margo --root sdk/tables build --check` was executed in isolated worktrees under identical build conditions:

- **Baseline (`ea7208964b29edb3363aa5cd58acf6d5321af833`):**
  Failed with `TYPE_MISMATCH` on test target semantic check:
  `assignment type mismatch for binding 'ignored'` at `sdk/tables/tests/tables_test.moss:151:1`
  (`semantic_identity`: `test:tests/tables_test.moss:constructor and schema errors are typed`).
- **Slice C branch (`10fb7361560418f36a321b15db4987835ac1fe53`):**
  Failed with identical `TYPE_MISMATCH` on test target semantic check:
  `assignment type mismatch for binding 'ignored'` at `sdk/tables/tests/tables_test.moss:151:1`
  (`semantic_identity`: `test:tests/tables_test.moss:constructor and schema errors are typed`).

**Classification:** Confirmed pre-existing debt on the baseline. Slice C introduces zero new failures or differences in `sdk/tables`. Proceeded with integration per L7 Conditional GO instructions.

---

## 3. Targeted Validation

All targeted gates were executed against the integrated tree in both default and `MOSS_EST=shadow` modes:

| Gate | Default Mode | EST Shadow Mode | Notes / Coverage |
|------|--------------|-----------------|------------------|
| Compiler Build (`moss-dev`, `moss-opt`) | **PASS** | **PASS** | Clean build with `clang++` and `mold` |
| `check_phase28_local_ownership.py` | **PASS** | **PASS** | Local effects, branch joins, while/for loop fixed points, Vector iteration, negatives, drop/perturb fail-closed |
| `check_phase28_selector_argument_effects.py` | **PASS** | **PASS** | READ/WRITE/CONSUME, exact EST identity, specialization isolation, unsupported trait dispatch fail-closed |
| `check_phase21_expression_ownership_edges.py` | **PASS** | **PASS** | Exceptional ownership edges pass |
| `check_enum_semantics.py` | **PASS** | **PASS** | 30/30 enum rejection cases pass |
| `check_enum_source_free.py` | **PASS** | **PASS** | Source-backed imported enum `.mossi` provider passes |
| `check_phase28_specialized_ownership_contract.py` | **PASS** | **PASS** | Typed exact lookup, 2 specializations, ambiguity, drop/perturb, unsupported |
| `check_phase28_specialized_native_consumer.py` | **PASS** | **PASS** | Multi-specialization, parity, 6 fail-closed modes, zero clones |
| `check_phase28_take_consumer.py` | **PASS** | **PASS** | All 22 take and selector semantic authority fail-closed tests pass |
| `check_phase28_team_b_consumer.py` | **PASS** | **PASS** | Read/Write/Consume lowering + E1 scalar temporary push + drop/perturb fail-closed |
| `verify_take_fixtures.py` | **PASS** (18/18) | **PASS** (18/18) | Checker, Fast Debug, Native, and Optimized Native parity verified with zero clones |
| `check_phase27_string_debt.py` | **PASS** | **PASS** | Ratchet preserved: 42 raw Stmt name comparisons, 7 binding comparisons |
| `verify_defects.py` | **PASS** (4 PASS, 4 KNOWN FAIL) | **PASS** | All 8 defect reproduction statuses confirmed; 0 unexpected failures |
| `check_agent_feedback.py` | **PASS** | **PASS** | Feedback schema validation passes |
| `git diff --check` | **PASS** | **PASS** | Clean diff hygiene |

---

## 4. Full Parallel Regression Suite Delta

Both the default and EST-shadow full parallel regression suites were executed across all 167 selected jobs:

### Default Suite Execution (`env -u MOSS_EST python3 tests/run_parallel.py`)
- **Total jobs:** 167
- **Passed:** 151
- **Failed:** 16
- **Skipped:** 0
- **Wall time:** 339.84s (5.7m)

### EST-Shadow Suite Execution (`MOSS_EST=shadow python3 tests/run_parallel.py`)
- **Total jobs:** 167
- **Passed:** 151
- **Failed:** 16
- **Skipped:** 0
- **Wall time:** 345.64s (5.8m)

### Reconciliation Against the 16 Non-Intermittent Baseline Ledger Failures

All 16 failed jobs in both default and shadow runs match the Round 3 ledger entries identically:

1. `check_phase21_addendum_a_c` (Intentional compatibility change: legacy `take(local)` rejected)
2. `check_phase21_on_fail_capture` (Intentional compatibility change: legacy `take(local)` rejected)
3. `check_phase21_recovery_semantics` (Intentional compatibility change: legacy `take(local)` rejected)
4. `check_phase21_recover_reinit` (Intentional compatibility change: legacy `take(local)` rejected)
5. `check_phase21_borrowed_view_write` (Preexisting failure: Phase 21 known debt)
6. `check_generic_specialization` (Incomplete Phase 28: nested `take(place)` expressions)
7. `check_phase26_1_c1_corpus_cases` (Intentional compatibility change: borrowed loop element consume rejected)
8. `check_phase25_team_a_semantics` (Intentional compatibility change: borrowed loop element consume rejected)
9. `check_phase25_s1_checker_authority` (Incomplete Phase 28: nested `take(place)` expressions)
10. `check_phase27_accepts_rustc` (Preexisting failure: Phase 27 negative tests downstream rustc)
11. `check_semantic_queries` (Incomplete Phase 28: `use_after_transfer` negative fixture)
12. `check_builtin_generic_typing` (Intentional compatibility change: legacy `take(local)` rejected)
13. `shard_negative` (Incomplete Phase 28: `use_after_transfer` negative fixture)
14. `shard_phase25_int_payloads` (Incomplete Phase 28: `use_after_transfer` negative fixture)
15. `shard_swarm_cases` (Intentional compatibility change: borrowed loop element consume rejected; shadow run reports known `put(take())` structural divergence reproduced on baseline `ea7208964`)
16. `shard_phase5_tooling` (Incomplete Phase 28: `use_after_transfer` negative fixture)

The intermittent test `check_make_incremental_dependencies` PASSED in both default (18.63s) and shadow (18.88s) runs.

### Regression Resolution Summary
- **New regressions:** 0.
- **Unexpected failures:** 0.
- **Skips:** 0.
- **String debt:** 42 raw Stmt name comparisons, 7 binding comparisons (meets the 42/7 threshold).
- **Loop facts:** Exact lookup and fail-closed behavior verified.
- **Clones:** Zero hidden clones verified.

---

## 5. Disposition and Closeout

- Integration of Phase 28 Round 4 Slice C into `phase28-work` is complete, verified, and clean.
- Merge commit: `528c4e46882a967100cebe4a363f2e321d0de94f`.
- Final HEAD: `528c4e46882a967100cebe4a363f2e321d0de94f` (or subsequent commit recording integration report and status).
- **STOP after integration. Do not merge to main or begin Slice D.**
