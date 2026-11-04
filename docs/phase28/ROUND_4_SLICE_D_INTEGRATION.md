# Phase 28 Round 4 Slice D Integration Report

## 1. Integration Record

- **Target branch:** `origin/phase28-work` at `6b0433c8af071dc972b2bd89109e6be0e498de9b`.
- **Source branch:** `origin/phase28-r4-slice-d` at `9aef4d2df6c83846f9bc00694a9a62713f2432ee`.
- **Approved implementation SHA:** `0c722497f2c6639cf84c96b80a9748ead9308b0a` (Agent 2 GO).
- **Source/test integrity verification:** `git diff 0c722497f2c6639cf84c96b80a9748ead9308b0a..9aef4d2df6c83846f9bc00694a9a62713f2432ee -- src/ tests/` is completely empty (zero source or test differences). The approved implementation is preserved exactly.
- **Integration merge commit:** `464f2a8fb7529ddbc3cdefa5359f1fb5d92aa3e3` (normal `--no-ff` merge into `phase28-work`).
- **Compiler/test edits during integration:** Strictly none. No compiler source or test expectations were modified.

---

## 2. Targeted Validation

All targeted gates were executed against the integrated tree in both default and `MOSS_EST=shadow` modes:

| Gate | Default Mode | EST Shadow Mode | Notes / Coverage |
|------|--------------|-----------------|------------------|
| Compiler Build (`moss-dev`, `moss-opt`) | **PASS** | **PASS** | Clean build with `clang++` and `mold` |
| `check_phase28_local_ownership.py` | **PASS** | **PASS** | Local effects, branch joins, while/for loops, Vector iteration, negatives, exceptional recovery, drop/perturb fail-closed |
| `check_phase21_source_free_standard_write_recovery.py` | **PASS** | **PASS** | Source-free standard-error WRITE Root recovery passes |
| `check_phase28_selector_argument_effects.py` | **PASS** | **PASS** | READ/WRITE/CONSUME, exact EST identity, specialization isolation, unsupported trait dispatch fail-closed |
| `check_phase21_expression_ownership_edges.py` | **PASS** | **PASS** | Expression-originated exceptional ownership edges pass |
| `check_enum_semantics.py` | **PASS** (30/30) | **PASS** (30/30) | 30/30 enum rejection cases pass |
| `check_enum_source_free.py` | **PASS** | **PASS** | Source-backed imported enum `.mossi` provider passes |
| `check_phase28_specialized_ownership_contract.py` | **PASS** | **PASS** | Typed exact lookup, 2 specializations, ambiguity, drop/perturb, unsupported |
| `check_phase28_specialized_native_consumer.py` | **PASS** | **PASS** | Multi-specialization, parity, 6 fail-closed modes, zero clones |
| `check_phase28_take_consumer.py` | **PASS** (22/22) | **PASS** (22/22) | All 22 take and selector semantic authority fail-closed tests pass |
| `check_phase28_team_b_consumer.py` | **PASS** | **PASS** | Read/Write/Consume lowering + E1 scalar temporary push + drop/perturb fail-closed |
| `verify_take_fixtures.py` | **PASS** (18/18) | **PASS** (18/18) | Checker, Fast Debug, Native, and Optimized Native parity verified with zero clones |
| `check_phase27_string_debt.py` | **PASS** | **PASS** | Ratchet preserved: 42 raw Stmt name comparisons, 7 binding comparisons |
| `verify_defects.py` | **PASS** (4 PASS, 4 KNOWN FAIL) | **PASS** | All 8 defect reproduction statuses confirmed; 0 unexpected failures |
| `check_agent_feedback.py` | **PASS** | **PASS** | Feedback schema validation passes |
| `git diff --check` | **PASS** | **PASS** | Clean diff hygiene |

---

## 3. Full Parallel Regression Suite Delta

Both the default and EST-shadow full parallel regression suites were executed across all 167 selected jobs:

### Default Suite Execution (`env -u MOSS_EST python3 tests/run_parallel.py`)
- **Total jobs:** 167
- **Passed:** 151
- **Failed:** 16
- **Skipped:** 0
- **Wall time:** 341.25s (5.7m)

### EST-Shadow Suite Execution (`MOSS_EST=shadow python3 tests/run_parallel.py`)
- **Total jobs:** 167
- **Passed:** 151
- **Failed:** 16
- **Skipped:** 0
- **Wall time:** 348.33s (5.8m)

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

The intermittent test `check_make_incremental_dependencies` PASSED in both default (21.13s) and shadow (21.33s) runs.

### Regression Resolution Summary
- **New regressions:** 0.
- **Unexpected failures:** 0.
- **Skips:** 0.
- **String debt:** 42 raw Stmt name comparisons, 7 binding comparisons (meets the 42/7 threshold).
- **Exceptional ownership facts:** Exact lookup and fail-closed behavior verified.
- **Clones:** Zero hidden clones verified.

---

## 4. Remaining Unsupported Cases

Consumer migration remains outside Slice D:
- General collection ownership, `copy`, expanded `take`, and `match consume` remain unsupported.
- Existing FileIO close obligations are preserved.
- Any pipeline operation without a complete checker-issued summary or exact stage occurrence fails closed; no source-text fallback, hidden copy, implicit take, or exception rollback is introduced.

---

## 5. Disposition and Closeout

- Integration of Phase 28 Round 4 Slice D into `phase28-work` is complete, verified, and clean.
- Merge commit: `464f2a8fb7529ddbc3cdefa5359f1fb5d92aa3e3`.
- Final HEAD: `464f2a8fb7529ddbc3cdefa5359f1fb5d92aa3e3` (or subsequent commit recording integration report and status).
- **STOP after integration. Do not merge to main or initiate consumer migration. Return the results to L7.**
