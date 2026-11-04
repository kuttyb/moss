# Phase 28 Independent Semantic Oracle Report

## 1. Executive Summary

> **Historical report notice (P28-R7-005):** The expectations in this report
> were superseded by approved L7 decisions. ORACLE-05's `option[V]` expectation
> was incorrect; its observed `Payload` result agrees with `Map.get(k) -> V`.
> ORACLE-07 incorrectly grouped Vector indexing panics with recoverable
> collection failures. ORACLE-08 tested a user `fn copy` as callable even
> though the revised contract reserves `copy`. The old run remains historical
> evidence only and is not a valid conformance result for those cases. The
> replacement table/addendum is pending L7 approval and is not frozen.

- **Task:** P28-R7-001 — Independent Semantic Oracle
- **L7 Decision:** GO. Phase 28 CP2 reopened. W12 integration suspended.
- **Base Commit:** `150ab36b41e45504aaa400aa717da241be7542fb` (`phase28-work`)
- **Branch:** `phase28-r7-oracle`
- **Specification Authority:** Frozen Ownership Proposal (`docs/MOSS_PHASE_28_LOL_OWNERSHIP_PEER_REVIEW_PROPOSAL.md` §10 and §12).
- **Scope & Results:**
  - 47 executable test fixtures across 8 categories.
  - Multi-engine execution: Checker, Fast Debug, Native Debug, Native Optimized (-O).
  - Clone budget audit: strictly 0 unauthorized hidden clones permitted.
  - Baseline `150ab36b` results: **19 PASS / 28 FAIL / 11 Hidden Clones**.
  - W12 commit `cfc9d739` results: **20 PASS / 27 FAIL / 21 Hidden Clones**.
  - All 10 concrete findings from the clean-room NO-GO review reproduced exactly.

---

## 2. Exact Reproduction of NO-GO Review Findings (at `cfc9d739`)

Every concrete failure identified during the clean-room review was independently reproduced by the oracle suite:

| # | Review Finding | Oracle Fixture ID | Expected (Frozen Spec §10/§12) | Actual (`cfc9d739`) | Actual (Base `150ab36b`) |
|---|---|---|---|---|---|
| 1 | Write-through borrowed Vector elements: expected 9,9 | `ORACLE-01-VEC-WRITE-THROUGH` | Native & FD output: `9\n9` | Fast Debug: `9\n9`; **Native: `9\n1`** (2 hidden clones) | Fast Debug: `9\n9`; **Native: `9\n1`** (2 hidden clones) |
| 2 | Indexed field reads without hidden clones | `ORACLE-02-INDEXED-FIELD-READ-NO-CLONE` | 0 hidden clones in lowering | **1 hidden clone** (`(v[0]).clone().tag`) | **1 hidden clone** (`(v[0]).clone().tag`) |
| 3 | Rejection of consuming borrowed collection elements | `ORACLE-03-COLL-ELEM-CONSUME-REJECT` | Checker REJECT (`OWNERSHIP_COLLECTION_ELEMENT_CONSUME`) | **UNEXPECTED_ACCEPT** (emitted `consume(v[0].clone())`) | **UNEXPECTED_ACCEPT** (emitted `consume(v[0].clone())`) |
| 4 | Write-through borrowed Map entries | `ORACLE-04-MAP-WRITE-THROUGH` | Native & FD output: `9\n9` | Fast Debug: `9\n9`; **Native: `9\n1`** (2 hidden clones) | Fast Debug: `9\n9`; **Native: `9\n1`** (2 hidden clones) |
| 5 | Borrowed `Map.get(key)` semantics | `ORACLE-05-MAP-GET-BORROW` | Returns borrowed `V`; missing key raises `CollectionError.MissingKey` | Historical fixture used an invalid `option[V]` expectation; checker returned `Payload`, which is consistent with the revised contract | Same historical oracle error |
| 6 | Native compilation of `match consume` on options | `ORACLE-06-MATCH-CONSUME-OPT-NATIVE` | Clean rustc compilation, output: `target` | Checker/FD pass; **rustc FAIL**: `error[E0433]: cannot find module or crate option` | **REJECTED**: `match requires an enum; found option[Box]` |
| 7 | Collection failures and Vector bounds | `ORACLE-07-CATCHABLE-COLL-ERRORS` | Missing Map keys, invalid removal indices and empty pops raise typed `CollectionError`; out-of-range `v[i]` panics | Historical fixture conflated these cases; its `remove(99)` failure is relevant, while a `v[i]` panic is expected | Same historical fixture limitation |
| 8 | Reserved `copy` declaration | `ORACLE-08-USER-COPY-RESOLUTION` | Reject user-defined `fn copy` | Historical fixture invoked a declaration now forbidden by the revised contract; output `5` is not a valid conformance verdict | Same historical oracle error |
| 9 | User-defined enum cases named `Some`/`None` | `ORACLE-09-USER-ENUM-SOME-NONE` | Matches user variant, output: `42` | Native: `42`; **FD CRASH: `interpreter error: missing enum payload`** | Native: `42`; **FD CRASH: `interpreter error: missing enum payload`** |
| 10a| Explicit-copy-only duplication | `ORACLE-10A-EXPLICIT-COPY-ONLY` | 1 authorized clone, 0 hidden clones | Total 3 clones: **2 hidden clones detected** | Total 3 clones: **2 hidden clones detected** |
| 10b| Rejection of source `.clone()` | `ORACLE-10B-CLONE-ALIAS-REJECTED` | Checker REJECT (`CLONE_ALIAS_UNSUPPORTED`) | **REJECTED** (`CLONE_ALIAS_UNSUPPORTED`) ✅ | **REJECTED** (`CLONE_ALIAS_UNSUPPORTED`) ✅ |

---

## 3. Comprehensive Failure Ledger (Unique IDs & Spec Citations)

The following ledger details all failures observed when evaluating against the defective compiler (`cfc9d739`):

| Failure ID | Fixture ID | Category | Defective Mode(s) | Observed Defect Summary | Specification Citation |
|---|---|---|---|---|---|
| `DEFECT-001` | `ORACLE-01-VEC-WRITE-THROUGH` | NO-GO Review | `Native, Native -O, Clones` | Lowering cloned `v[0]`; mutation didn't write through (got `9\n1`, expected `9\n9`); 2 hidden clones | Proposal §10 row `b = v[0]; b.cost = 9`, §1.2, §5.3 |
| `DEFECT-002` | `ORACLE-02-INDEXED-FIELD-READ-NO-CLONE` | NO-GO Review | `Clones` | Lowering emitted `(v[0]).clone().tag` instead of borrowing projection; 1 unauthorized clone | Proposal §1.2, §1.3, §10, §12 |
| `DEFECT-003` | `ORACLE-03-COLL-ELEM-CONSUME-REJECT` | NO-GO Review | `Checker` | Checker accepted consuming borrowed `v[0]` without explicit copy; inserted hidden clone in lowering | Proposal §10 row `nxt.push(head[0])`, §5.3 |
| `DEFECT-004` | `ORACLE-04-MAP-WRITE-THROUGH` | NO-GO Review | `Native, Native -O, Clones` | Lowering cloned `m["key"]`; mutation didn't write through (got `9\n1`, expected `9\n9`); 2 hidden clones | Proposal §10, §5.3, §12 |
| `DEFECT-005` | `ORACLE-05-MAP-GET-BORROW` | Historical oracle false positive | `Checker` | Prior oracle incorrectly required `option[V]`; observed borrowed `Payload` is consistent with revised `Map.get(k) -> V` semantics. Retained as a permanent historical ID; no new defect is assigned. | P28-R7-005 addendum draft |
| `DEFECT-006` | `ORACLE-06-MATCH-CONSUME-OPT-NATIVE` | NO-GO Review | `Native, Native -O` | Native generator emitted `option::Some` instead of `Option::Some`; rustc compilation failed with E0433 | Proposal §6.2, §10, §12 |
| `DEFECT-007` | `ORACLE-07-CATCHABLE-COLL-ERRORS` | Historical mixed finding | `FD, Native, Native -O` | The invalid `remove(99)` result remains a typed-error conformance question; old broad catch cannot determine its variant and old indexing language was incorrect. Re-run the corrected operation matrix. | P28-R7-005 addendum draft |
| `DEFECT-008` | `ORACLE-08-USER-COPY-RESOLUTION` | Historical oracle false positive | `Checker, FD, Native` | Old fixture expected a user-defined `copy` call to work. Revised contract reserves this name, so the correct test rejects the declaration. Retained as a permanent historical ID. | P28-R7-005 addendum draft |
| `DEFECT-009` | `ORACLE-09-USER-ENUM-SOME-NONE` | NO-GO Review | `Fast Debug` | Fast Debug special-cased variant name `Some` to built-in option, crashing with `missing enum payload` | Proposal §6, §10, §12 |
| `DEFECT-010` | `ORACLE-10A-EXPLICIT-COPY-ONLY` | NO-GO Review | `Clones` | Lowering inserted 2 unauthorized hidden clones alongside authorized `copy(x)` | Proposal §10 row `copy(v[0])`, §11 Phase D |
| `DEFECT-011` | `ORACLE-14-ASSIGN-CONSUME-BORROW-REJECT` | Assignment & Locals | `Checker` | Checker accepted returning borrowed collection element `v[0]`, inserting unauthorized hidden clone | Proposal §10 row `b = a; return b`, §3 |
| `DEFECT-012` | `ORACLE-15-PLAIN-FIELD-CONSUME-REJECT` | Assignment & Locals | `Checker` | Checker accepted passing non-movable plain field to consuming function without `take` | Proposal §10 row `b = state.frontier; archive(b)`, §5.2 |
| `DEFECT-013` | `ORACLE-17-TAKE-OPTION-FIELD` | Take Moves & Restrictions | `Native, Native -O` | rustc failed with E0433 (`cannot find module option`) on lowering `option::Some` in match | Proposal §10 row `b = take(job.pending)`, §5.1 |
| `DEFECT-014` | `ORACLE-19-TAKE-NESTED-VEC-OPTION` | Take Moves & Restrictions | `Native, Native -O, Clones` | rustc failed with E0433 on option pattern; 2 unauthorized hidden clones in projection lowering | Proposal §10 row `take(v[i].pending)`, §5.1 |
| `DEFECT-015` | `ORACLE-20-TAKE-NESTED-MAP-VECTOR` | Take Moves & Restrictions | `Clones` | Lowering emitted 2 unauthorized hidden clones during map projection and take | Proposal §10 row `take(m[k].items)`, §5.1 |
| `DEFECT-016` | `ORACLE-26-VEC-REMOVE-API` | Rebinding & Loops | `Clones` | Lowering inserted 2 unauthorized hidden clones during vector removal and push | Proposal §10 row `nxt.push(head.remove(0))`, §5.3 |
| `DEFECT-017` | `ORACLE-27-REBIND-AFTER-BORROWS` | Rebinding & Loops | `Native, Native -O` | Native generator typed local as `&Box` across whole scope; rebind with owned `Box` caused rustc E0308 | Proposal §10 row `b = a; b = new_value`, §3.1 |
| `DEFECT-018` | `ORACLE-30-JOIN-BORROW-FRESH-REJECT` | Joins | `Checker` | Checker accepted joining borrowed element with fresh owned constructor at live join | Proposal §10 row `if c: b = left[0] else: b = Fresh()`, §4 |
| `DEFECT-019` | `ORACLE-31-JOIN-PROMOTED-CONSUME` | Joins | `Native, Native -O` | Native generator lowered join to borrow view; rustc E0308 on promoted consume | Proposal §10 row `if c: b = left else: b = Fresh()`, §4 |
| `DEFECT-020` | `ORACLE-32-MATCH-PAYLOAD-WRITE` | Patterns & Match Consume | `Native, Native -O, Clones` | Lowering emitted 2 hidden clones and failed to emit mutable pattern borrow; rustc E0596 | Proposal §10 row `match job.state and mutate a payload`, §6.1 |
| `DEFECT-021` | `ORACLE-33-MATCH-LAST-USE-OVERWRITE` | Patterns & Match Consume | `Checker, FD, Native` | Checker held READ borrow of scrutinee for whole match; rejected overwrite after last payload use | Proposal §10 row `match job.state then overwrite state`, §6.1 |
| `DEFECT-022` | `ORACLE-40-VEC-FOR-WRITE` | Collections & Iteration | `Native, Native -O, Clones` | Emitter emitted `for item in &v` and `bump(&mut item)`, causing rustc E0596 (Defect E4); 2 hidden clones | Proposal §10 row `for item in items: item.bump()`, §5.3, §12 |
| `DEFECT-023` | `ORACLE-42-CUSTOM-WRAPPER-REMOVE` | Collections & Iteration | `Clones` | Lowering inserted 1 unauthorized hidden clone during wrapper return | Proposal §10 row `User-defined wrapper fn remove_at`, §9 |
| `DEFECT-024` | `ORACLE-43-HANDLER-PRIMITIVE-REPLY` | Handlers & Messages | `Clones` | Lowering inserted 1 unauthorized clone during handler message dispatch | Proposal §10 row `reply n where n: Int`, §7.5 |
| `DEFECT-025` | `ORACLE-45-HANDLER-PRIMITIVE-REBIND-LOCAL` | Handlers & Messages | `Clones` | Lowering inserted 1 unauthorized clone during handler local rebound copy | Proposal §10 row `result = n; result = result + 1; reply result`, §7.5 |
| `DEFECT-026` | `ORACLE-46-HANDLER-NONPRIM-REPLY-REJECT` | Handlers & Messages | `Checker` | Checker accepted replying non-primitive incoming parameter without explicit `copy(x)` | Proposal §10 row `reply payload where payload: Report`, §7.5 |

---

## 4. Metrics & Clone Budgets

### Acceptance & Compliance Summary
| Metric | Baseline `150ab36b` | W12 Commit `cfc9d739` | Specification Goal |
|---|---|---|---|
| **Total Test Fixtures** | 47 | 47 | 47 |
| **Fully Compliant (PASS)** | 19 (40.4%) | 20 (42.6%) | 47 (100%) |
| **Defective / Non-Compliant (FAIL)** | 28 (59.6%) | 27 (57.4%) | 0 (0%) |
| **Unauthorized Hidden Clones** | 11 | 21 | 0 |
| **Rustc Compilation Errors** | 4 | 7 | 0 |
| **Engine Parity Divergences** | 5 | 5 | 0 |

### Performance Measurements (Wall-Clock Time)
- **Suite Execution Time:** ~14.2s (running all 47 fixtures through Checker, Fast Debug, Native Debug, and Native Optimized).
- **Average Per-Fixture Check Time:** ~0.025s
- **Average Fast Debug Interpretation Time:** ~0.031s
- **Average Native Lowering & Rustc Compile Time:** ~0.240s
- **Memory Footprint (RSS):** Well within compiler baselines (peak RSS < 45 MB).

---

## 5. Frozen Implementation SHA & Integrity Assurance

This section describes only the original P28-R7-001 historical snapshot. It
does not freeze the revised P28-R7-005 oracle. Revised freeze requires L7
approval, independent harness review, and the exact post-approval delivery SHA.

- **Harness Path:** `tests/phase28/oracle/oracle_harness.py`
- **Fixtures Directory:** `tests/phase28/oracle/fixtures/` (47 files)
- **Branch:** `phase28-r7-oracle`
- **Integrity Invariant:**
  - Zero modifications to compiler code (`src/` is 100% clean diff).
  - No expectations weakened or altered to appease compiler defects.
  - Failures reflect real non-compliance against the frozen specification.
