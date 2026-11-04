# Phase 28 Team B W08–W11 Consumer Migration Integration Report

## 1. Integration Record

- **Target branch:** `origin/phase28-work` at `887cb15d87a3be532381aa5357d10bab0c66c87f`.
- **Source branch:** `phase28-r5-team-b-consumers` at `62ff748a25baed6159bf2c3aabf318c7237d2685`.
- **Approved implementation SHA:** `62ff748a25baed6159bf2c3aabf318c7237d2685` (Independent Agent 2 CP2 GO).
- **Integration merge commit:** `06e1640798e16ea770c0ba13e9a4f4efb71f92e5` (normal `--no-ff` merge into `phase28-work`).
- **Compiler/test edits during integration:** Strictly none. No compiler source or test expectations were modified during integration.

---

## 2. Scope & Migrated Decision Classes

Under L7 consolidated authorization, Team B executed W08 through W11 sequentially:

1. **W08 Normal-Flow Ownership Migration:**
   - Native generator and Fast Debug interpreter consume authoritative checker facts (`TypedLocalOwnershipFactV1` and `SemanticFacts.inferred_effect`).
   - Replaced custom ownership heuristics with fail-closed checker fact queries (`find_local_ownership_fact`).
   - Handled per-definition READ (`&origin`), WRITE (`&mut origin`), and CONSUME (value transfer) lowering.
   - Handled rebinding and killed definitions without corrupting live borrow references.
   - Handled write-through assignments (`write_through_parameters_`) preserving target place references.
   - Handled branch and match reaching-definition joins with un-annotated mutable join headers.

2. **W09 Loop-Carried Ownership Migration:**
   - Loop condition and iterable handles resolve required loop occurrences.
   - While/for loop backedges reinitialize available bindings governed strictly by `TypedLoopOwnershipFactV1` with `EdgeKind::Backedge` and `ValueState::Available`.
   - Vector iteration mutability (.iter_mut() vs .iter()) is decided strictly by `fact->converged_access == Effect::Write`.
   - Consumer resolution filters for present facts (`req.availability == Present`).

3. **W10 Exceptional-Flow Ownership Migration:**
   - Emitter and Fast Debug resolve authoritative `TypedExceptionalOwnershipFactV1` records.
   - State-at-raise (`bs.borrow_source_binding_identities` / `bs.borrow_storage_paths`) informs declaration boundaries to prevent uninitialized or corrupted variable use across exceptional recovery.
   - Echo argument evaluation buffering ensures exact left-to-right argument evaluation order without rollback.
   - Fail-closed behavior on missing or contradictory exceptional facts.

4. **W11 Optimizer & Lowering Parity:**
   - Confirmed functional optimizer uses checker node ownership facts for pipeline fusion safety.
   - Fixed defect E1 scalar Copy push and field self-assignment lowering.
   - Fixed defect E4 Fast Debug loop mutation write-through alias preservation.
   - Established complete 3-way output parity across Fast Debug (`moss run --interp`), Native Debug (`rustc`), and Native Optimized (`rustc -O`).
   - Zero hidden nonprimitive clones introduced across all lowering paths.

---

## 3. Targeted Validation

All targeted gates were executed against the integrated tree in `tmp/integration-wt`:

| Gate | Result | Notes / Coverage |
|------|--------|------------------|
| Compiler Build (`moss-dev`, `moss-opt`) | **PASS** | Clean build with `clang++` and `mold` |
| `check_phase28_team_b_consumer.py` | **PASS** | Read/Write/Consume lowering, E1 push, drop/perturb/delete fail-closed, specialization isolation, 3-way parity |
| `check_phase28_local_ownership.py` | **PASS** | Local effects, branch joins, while/for loops, Vector iteration, negatives, drop/perturb fail-closed |
| `check_phase28_selector_argument_effects.py` | **PASS** | READ/WRITE/CONSUME, exact EST identity, specialization isolation, unsupported trait dispatch fail-closed |
| `check_phase28_specialized_ownership_contract.py` | **PASS** | Typed exact lookup, 2 specializations, ambiguity, drop/perturb, unsupported |
| `check_phase28_take_consumer.py` | **PASS** (22/22) | All 22 take and selector semantic authority fail-closed tests pass |
| `verify_take_fixtures.py` | **PASS** (18/18) | Checker, Fast Debug, Native, and Optimized Native parity verified with zero clones |
| `verify_defects.py` | **PASS** (4 PASS, 4 KNOWN FAIL) | All 8 defect reproduction statuses confirmed; 0 unexpected failures |
| `check_phase27_string_debt.py` | **PASS** | Ratchet preserved: 42 raw Stmt name comparisons, 7 binding comparisons (<= 42/7 threshold) |
| `check_agent_feedback.py` | **PASS** | Feedback schema validation passes |
| `git diff --check` | **PASS** | Clean diff hygiene |

---

## 4. Full Parallel Regression Suite Delta

Both the default and EST-shadow full parallel regression suites were executed across all 167 selected jobs in the integrated tree:

### Default Suite Execution (`env -u MOSS_EST python3 tests/run_parallel.py`)
- **Total jobs:** 167
- **Passed:** 151
- **Failed:** 16
- **Skipped:** 0
- **Wall time:** 342.10s

### EST-Shadow Suite Execution (`MOSS_EST=shadow python3 tests/run_parallel.py`)
- **Total jobs:** 167
- **Passed:** 151
- **Failed:** 16
- **Skipped:** 0
- **Wall time:** 349.52s

### Reconciliation Against the 16 Baseline Ledger Failures

All 16 failed jobs in both default and shadow runs match the Round 3/4 baseline ledger entries identically:

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

The intermittent test `check_make_incremental_dependencies` PASSED in both default (21.43s) and shadow (21.78s) runs.

### Regression Resolution Summary
- **New regressions:** 0.
- **Unexpected failures:** 0.
- **Skips:** 0.
- **String debt:** 42 raw Stmt name comparisons, 7 binding comparisons (meets the 42/7 threshold).
- **Exceptional ownership facts:** Exact lookup and fail-closed behavior verified.
- **Clones:** Zero hidden nonprimitive clones verified.

---

## 5. Disposition and Closeout

- Implementation frozen at `62ff748a25baed6159bf2c3aabf318c7237d2685` and approved by independent Agent 2 (Verdict: GO).
- Integrated into `phase28-work` at merge commit `06e1640798e16ea770c0ba13e9a4f4efb71f92e5`.
- Full integration validation confirms zero new regressions against the 16-failure baseline.
- **No merge to `main`. Team B consumer migration complete. Results returned to L7.**
