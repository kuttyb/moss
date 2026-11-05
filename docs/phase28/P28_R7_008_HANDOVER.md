# P28-R7-008 — Handover Document for Next Agent Team

**Date:** 2026-11-04
**Active Branch:** `phase28-r7-008-integration` (pushed to `origin/phase28-r7-008-integration`)
**Implementation Commit SHA:** `e9d3f844bae715809f6b78f4d0d0f8ba908db6bc`
**Full Technical Report:** [`docs/phase28/P28_R7_008_INTEGRATION_REPORT.md`](P28_R7_008_INTEGRATION_REPORT.md)

---

## 1. Executive Summary & Current State

All implementation, reconciliation, oracle evaluation, and full `make check` regression gates for **P28-R7-008 — Phase 28 Controlled Integration (Staging integration only)** are **COMPLETE** and pushed to `origin/phase28-r7-008-integration`.

- **What is completed:**
  1. **Branch Construction & Provenance:** Created `phase28-r7-008-integration` from `phase28-work` (`150ab36b41e45504aaa400aa717da241be7542fb`), merged oracle handoff `46619968f64ec618f4cc2576f601b86569399f3c` (certified oracle `544506774211fbb5b9d4d3e8116525b389faa3dd`) and reviewed implementation `c32bdddd2a364d156bfac9bb2160ef9c89dc9ce7` (R7-003 `480fa258`, R7-006 `76af12a6`, R7-007 `c32bdddd`).
  2. **Certified Oracle Preservation:** `git diff 46619968f64ec618f4cc2576f601b86569399f3c HEAD -- tests/phase28/oracle docs/phase28/P28_R7_005_ORACLE_CERTIFICATION.md docs/phase28/P28_R7_005_LANGUAGE_ADDENDUM.md` is **0 lines**. Digest pins match; `python3 tests/phase28/oracle/test_oracle_harness.py` passes **16/16**.
  3. **Reconciliation of W12 & R7-006 Inherited Changes (`d08aa7e8` & `e9d3f844`):**
     - **1-arg `Map.get(key)` semantics:** `src/moss.cpp:673` returns `$value` (`V`), matching `ORACLE-05`; corrected false `option[V]` claim in `docs/phase28/TEAM_A_W12_CHECKER_HANDOFF.md:96`; updated `tests/tooling/fixtures/builtin_generic_typing/map_get_arity.moss` to test 0-arg `m.get()`.
     - **User-defined `copy` shadowing:** Reserved `copy` as a keyword in `src/semantic_checker.cpp`, rejecting user declarations (`fn`, `type`, `enum`, `domain`, `trait`) named `copy` with `RESERVED_KEYWORD` (`ORACLE-08-USER-COPY-RESOLUTION` now **PASSES**).
     - **Checker-side text parsing (`starts_with` / `substr`):** Excluded W12 raw string prefix slicing in `inferred_expr_core` and `ownership_take_cursor` (`src/semantic_checker.cpp`) and replaced with EST node inspection (`checker_est_view` / `est::Take`) and `tree_parse_simple_call` (`copy`). String debt remains at `42` / `7`.
     - **Unauthorized W12 consumer edits:** Audited and classified in `docs/phase28/P28_R7_008_INTEGRATION_REPORT.md` (W12 `match` lowering replaced by R7-003 `TypedMatchOwnershipFactV1`; `copy(x)` and collection remove/get lowering retained with residual clone-provenance sidecar debt documented).
     - **22 Inherited `make check` Regressions Resolved:** Scoped R7-006 indexed local borrow definitions and `OWNERSHIP_COLLECTION_ELEMENT_CONSUME` via `indexed_borrow_storage_location` and `is_current_object_field` (excluding `String` indexing, object fields, and existing-local reassignments); clamped indexed local borrow definitions to `Effect::Read` (`ORACLE-14` **PASSES**); restored `Effect::Read` on index base in `check_ownership_expression_walk` (`check_phase28_selector_argument_effects.py` **PASSES**); restored arity fallback in `validate_builtin_argument_types`; skipped `ownership_take_cursor` when a user `fn take` exists; borrowed indexed member projections in `Echo` and `read_call_place` (`ORACLE-02` zero clones) while cloning non-Copy projections when evaluated as owned values in `expr()`.
  4. **Integration Gates & Full Regression Suite:**
     - **Certified 56-Fixture Oracle:**
       - Passed all applicable gates (`status == PASS`): **8 -> 11 (+3: `ORACLE-08`, `ORACLE-10B`, `ORACLE-14`)**
       - Semantic pass (`semantic_status == PASS`): **22 -> 38 (+16: `ORACLE-01`, `04`, `05`, `06`, `08`, `10A`, `10B`, `14`, `16`, `17`, `18`, `19`, `20`, `26`, `40`, `42`)**
       - Semantic failures (`semantic_status == FAIL`): **34 -> 18 (-16)**
       - Blocked by missing v2 `.moss_clone_provenance.json` sidecar (`clone_provenance_status == BLOCKED_MISSING_OR_INVALID`): **19 -> 29** (27 of which pass all 4 semantic/execution gates)
     - **Full `make --jobs="$(nproc)" check`:** **166 of 167 jobs PASS, 1 pre-existing FAIL (`check_phase21_borrowed_view_write`), 0 SKIP (353.72s)** — **ZERO regressions** relative to `phase28-work` (`150ab36b`).

---

## 2. Immediate Next Steps for the Receiving Agent Team

### Step 1: Complete Independent Agent 2 Review on `phase28-r7-008-integration`
The staging implementation is frozen at `e9d3f844bae715809f6b78f4d0d0f8ba908db6bc` (only this handover doc commit sits on top of it).
- Run Independent Agent 2 review over `e9d3f844bae715809f6b78f4d0d0f8ba908db6bc` (or `HEAD` of `phase28-r7-008-integration`) to certify **GO** on P28-R7-008 Step 4.
- Record the Agent 2 GO verdict in `docs/phase28/P28_R7_008_INTEGRATION_REPORT.md` and `.codex/CURRENT_STATUS.md`, commit, and push to `origin/phase28-r7-008-integration`.
- **Hard Boundary Reminder:** Do **NOT** merge `phase28-r7-008-integration` into `phase28-work` or `main` without separate L7 authorization.

### Step 2: Follow-Up Workstreams to Close Remaining Phase 28 Oracle Gaps (Once L7 Authorizes)
After L7 reviews the staging integration report (`docs/phase28/P28_R7_008_INTEGRATION_REPORT.md`), the remaining Phase 28 oracle work divides cleanly into two tracks:

1. **Track A — Native v2 `.moss_clone_provenance.json` Sidecar Emission (Unblocks 27 Passing Semantic Fixtures Immediately):**
   - Implement v2 `<binary>.moss_clone_provenance.json` sidecar emission in `src/native_generator.cpp` and wire `TypedCopyFactV1` (`find_copy_fact`) to `copy(x)` lowering (`src/native_generator.cpp:4869`).
   - Change 1-arg `Map.get(k)` native lowering (`src/native_generator.cpp:4877`) from `.get(&(k)).cloned().unwrap_or_else(...)` to borrow in place when used as a read projection (`ORACLE-05`).
   - Once the v2 sidecar is emitted and unauthorized `.clone()` calls are removed, up to **27 fixtures** that already pass all 4 semantic/execution modes (`checker`, `fast_debug`, `native_debug`, `native_opt`) will flip from `BLOCKED_MISSING_OR_INVALID` to full `PASS`.

2. **Track B — Remaining 18 Semantic Oracle Failures:**
   - **5 fixtures (`ORACLE-07`, `48`, `53`, `54`, `55`):** Register `CollectionError` (`OutOfBounds`, `MissingKey`, `Empty`) in `standard_errors` and infer `CollectionError` raise effects on `Vector.remove`, `Vector.swap_remove`, `Map.remove`, `Map.get(key)`, and `Queue.pop()`.
   - **5 fixtures (`ORACLE-03`, `28`, `30`, `34`, `35`):** Align checker diagnostic codes (`OWNERSHIP_USE_AFTER_REBIND`, `OWNERSHIP_MIXED_JOIN`, `OWNERSHIP_CONFLICTING_ACCESS`, `OWNERSHIP_ILLEGAL_CONSUME`) with `tests/phase28/oracle/diagnostic_mapping.json` (note: `ORACLE-03` already emits `OWNERSHIP_COLLECTION_ELEMENT_CONSUME` at line 10, whereas frozen `diagnostic_mapping.json` has `source_line_regex: "push|head\\[0\\]"` which only appears in line 1's comment).
   - **2 fixtures (`ORACLE-32`, `33`):** Match borrowed scrutinee payload mutation (`ORACLE-32`) and last-use borrow liveness inside match arms (`ORACLE-33`).
   - **4 fixtures (`ORACLE-27`, `31`, `49`, `51`):** Native Rust reference-to-owned coercion at rebind/join (`ORACLE-27`, `51`), join promotion when one branch consumes (`ORACLE-31`), and `try`/`recover` move tracking before first fallible call (`ORACLE-49`).
   - **2 fixtures (`ORACLE-15`, `46`):** Reject implicit consume of plain non-Copy record field without `take` (`ORACLE-15`) and reject non-primitive borrowed reply from domain handler (`ORACLE-46`).

---

## 3. Quick Verification Commands for Receiving Team

```bash
# 1. Verify branch and clean state
git status -sb
git log --oneline -n 6

# 2. Verify certified oracle is untouched and 16/16 harness tests pass
git diff 46619968f64ec618f4cc2576f601b86569399f3c HEAD -- tests/phase28/oracle docs/phase28/P28_R7_005_ORACLE_CERTIFICATION.md docs/phase28/P28_R7_005_LANGUAGE_ADDENDUM.md
python3 tests/phase28/oracle/test_oracle_harness.py

# 3. Run targeted Phase 28 and hygiene suites
python3 tests/tooling/check_phase28_w12_checker.py ./moss
python3 tests/tooling/check_phase28_r7_003_consumers.py ./moss
python3 tests/tooling/check_phase28_r7_006_checker_facts.py ./moss
python3 tests/tooling/check_phase28_selector_argument_effects.py ./moss
python3 tests/tooling/check_phase27_string_debt.py
python3 tests/tooling/check_agent_feedback.py

# 4. Run full 56-fixture oracle
python3 tests/phase28/oracle/oracle_harness.py --moss-bin ./moss --output-json tmp/oracle_results.json --output-md tmp/oracle_summary.md
```
