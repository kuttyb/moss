# P28-R7-008 — Phase 28 Controlled Staging Integration Report

**Status:** COMPLETE — Independent Agent 2 Review **GO** on frozen implementation SHA `e9d3f844bae715809f6b78f4d0d0f8ba908db6bc` (staging branch `phase28-r7-008-integration`; **NOT merged** to `phase28-work` or `main`).

## 1. Branch & Input Provenance

| Artifact / Input | Commit SHA | Role |
|---|---|---|
| Base (`phase28-work`) | `150ab36b41e45504aaa400aa717da241be7542fb` | Staging branch base (includes W13 `31f744fc` on top of `2a60693c`) |
| Certified Oracle | `544506774211fbb5b9d4d3e8116525b389faa3dd` | Frozen 56-fixture oracle, harness, expectations, allowlist, and certification |
| Oracle Handoff | `46619968f64ec618f4cc2576f601b86569399f3c` | Closeout handoff record for certified oracle (`phase28-r7-005`) |
| Reviewed Implementation | `c32bdddd2a364d156bfac9bb2160ef9c89dc9ce7` | Reviewed R7-003 (`480fa258`), R7-006 (`76af12a6`), and R7-007 (`c32bdddd`) repairs |
| Frozen Staging Implementation | `e9d3f844bae715809f6b78f4d0d0f8ba908db6bc` | Reconciled staging implementation certified **GO** by Independent Agent 2 |

---

## 2. Certified Oracle Preservation & Verification

All frozen oracle files from `544506774211fbb5b9d4d3e8116525b389faa3dd` / `46619968f64ec618f4cc2576f601b86569399f3c` (`tests/phase28/oracle/**`, `docs/phase28/P28_R7_005_ORACLE_CERTIFICATION.md`, `docs/phase28/P28_R7_005_LANGUAGE_ADDENDUM.md`) are preserved bit-for-bit unchanged on `phase28-r7-008-integration`:

- `git diff 46619968f64ec618f4cc2576f601b86569399f3c HEAD -- tests/phase28/oracle docs/phase28/P28_R7_005_ORACLE_CERTIFICATION.md docs/phase28/P28_R7_005_LANGUAGE_ADDENDUM.md`: **empty (`0` diff lines)**.
- `tests/phase28/oracle/expectations.json` SHA-256:
  `223a15436046767175de9c1f30637bb80c76505cb402f5dadb700c64520f4431` (matches `EXPECTED_EXPECTATIONS_SHA256`).
- `tests/phase28/oracle/runtime_callsite_allowlist.json` SHA-256:
  `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e19d988c0` (matches `EXPECTED_ALLOWLIST_SHA256`).
- Oracle harness unit test suite (`python3 tests/phase28/oracle/test_oracle_harness.py`): **16/16 PASS**.

---

## 3. Inherited Change Audit & Reconciliation Classification

Ancestry from `2a60693c` to `c32bdddd`:
- `cfc9d739` / `57b65dfc`: Team A W12 (`phase28-r6-w12-checker`) — suspended due to consumer lease violation and semantic defects.
- `480fa258`: Team B P28-R7-003 (`phase28-r7-003-consumers`) — authorized consumer repairs.
- `76af12a6`: Team B P28-R7-006 (`phase28-r7-006-checker-facts`) — authorized checker fact completion.
- `c32bdddd`: Team B P28-R7-007 (`phase28-r7-007-collection-take`) — authorized nested indexed `take(place)` repairs.

### Classification of Inherited Changes (`2a60693c..c32bdddd`)

| Component / Change | Source Commit(s) | Classification | Disposition & Technical Rationale |
|---|---|---|---|
| **1. `Map.get(key)` 1-arg semantics** (`src/moss.cpp:673`, `docs/phase28/TEAM_A_W12_CHECKER_HANDOFF.md:96`) | `cfc9d739` (W12) | **REQUIRED (code) / REPLACED (doc)** | `src/moss.cpp:673` registers 1-arg `Map.get(key)` returning `$value` (`V`) with `Effect::Read`, which matches `ORACLE-05-MAP-GET-BORROW` (`m.get("a").x`). However, `TEAM_A_W12_CHECKER_HANDOFF.md:96` falsely claimed 1-arg `Map.get(key)` returns `option[V]`; that false handoff documentation was **replaced** in `d08aa7e8`. Residual native clone debt on 1-arg `Map.get` is tracked in Section 4. |
| **2. User-defined `copy` shadowing** (`src/semantic_checker.cpp`, `ORACLE-08-USER-COPY-RESOLUTION`) | `cfc9d739` (W12) | **REPLACED / REPAIRED** | W12 intercepted `copy(x)` calls as `builtin:copy-v1` without rejecting user declarations named `copy`, silently shadowing user `fn copy(...)` and failing `ORACLE-08` (`UNEXPECTED_ACCEPT`). Repaired in `d08aa7e8` (`src/semantic_checker.cpp:580-618, 703`): user declarations (`fn`, `type`, `enum`, `domain`, `trait`) named `copy` are now rejected with `RESERVED_KEYWORD` (`ORACLE-08` now **PASSES**). |
| **3. Checker-side text parsing (`starts_with` / `substr` slicing)** (`src/semantic_checker.cpp:12307-12318, 25559-25561`) | `cfc9d739` (W12) | **EXCLUDED & REPLACED** | W12 added raw string prefix/suffix slicing (`starts_with(original, "take(")` / `starts_with(original, "copy(")`) in `inferred_expr_core` and `ownership_take_cursor`. Excluded and replaced in `d08aa7e8` with EST node inspection (`checker_est_view` checking `est::Take`) and `tree_parse_simple_call` for `copy`. |
| **4a. Unauthorized W12 consumer edits: `match` / `match consume` lowering** (`src/interpreter.hpp`, `src/native_generator.cpp`) | `cfc9d739` (W12) -> `480fa258` (R7-003) | **REPLACED** | W12's unauthorized and buggy `match` lowering (which broke user enums defining `Some`/`None` in `ORACLE-09` and panicked on `match consume` over `option[T]` in `ORACLE-06`) was replaced by Team B in R7-003 (`480fa258`) using checker-issued `TypedMatchOwnershipFactV1` (`resolve_match_ownership_fact`). |
| **4b. Unauthorized W12 consumer edits: `copy(x)` and `Vector.remove` / `swap_remove` / `Map.remove` / `Map.get` runtime & native lowering** (`src/interpreter.hpp`, `src/native_generator.cpp`, `src/moss.cpp`) | `cfc9d739` (W12), audited in `480fa258` (R7-003) | **REQUIRED (with tracked residual debt)** | Required for `ORACLE-05`, `ORACLE-10A`, `ORACLE-26`, `ORACLE-42`, and `check_phase28_w12_checker.py`. Retained after R7-003 consumer verification; residual native clone-provenance sidecar debt is documented in Section 4. |
| **5. R7-006 index-base effect propagation & borrowed-element consume site** (`src/semantic_checker.cpp`) | `76af12a6` (R7-006) | **REPLACED / REPAIRED** | R7-006 passed `requested` (`Effect::Consume`) instead of `Effect::Read` to index base `base` in `check_ownership_expression_walk` (breaking `check_phase28_selector_argument_effects.py` when reading Copy sub-projections of non-Copy vector elements) and emitted `OWNERSHIP_COLLECTION_ELEMENT_CONSUME` at the borrow definition `borrowed = v[0]` instead of clamping the borrow definition to `Effect::Read` so consuming uses fail at the consume site with `OWNERSHIP_ILLEGAL_CONSUME` (`ORACLE-14`). Additionally, R7-006 over-broadly treated `String` indexing, object field indexing, and existing-local reassignments as indexed local borrows, and rejected consuming `String`/sub-projection indexed expressions in call arguments. Repaired via `indexed_borrow_storage_location` and `is_current_object_field`: both `check_phase28_selector_argument_effects.py`, `ORACLE-14`, and all 22 inherited `make check` regressions now **PASS**. |
| **6. W12 arity-lookup fallback & user-declared `take` shadowing** (`src/semantic_checker.cpp`) | `cfc9d739` (W12) | **REPLACED / REPAIRED** | W12's `validate_builtin_argument_types` skipped the `builtin_operation(receiver_type, method)` fallback when arity-specific lookup returned `nullptr` (suppressing wrong-arity diagnostics), and `ownership_take_cursor` treated calls to a user-declared `fn take(...)` as built-in `est::Take`. Repaired in `src/semantic_checker.cpp`; updated `tests/tooling/fixtures/builtin_generic_typing/map_get_arity.moss` (`m.get("a")` -> `m.get()`) and `tests/tooling/check_phase15_15_express005_followups.py` (`D9` control case) to reflect Phase 28 1-arg `Map.get(key)` and native `option[T]` `match` support. |
| **7. Native indexed member projection borrowing vs owned cloning** (`src/native_generator.cpp`) | `480fa258` (R7-003) | **REPLACED / REPAIRED** | R7-003 suppressed `.clone()` on all `MemberAccess` over `Index` (`v[k].label`) inside `expr()`, causing `rustc` E0308/E0507 when `v[k].label` was passed to a consuming call (`parts.push(v[k].label)` in `check_phase25_s3_key_protocol.py`). Repaired by borrowing indexed member projections in `Echo` and `read_call_place` (`ORACLE-02` has zero `.clone()`), while cloning non-Copy field projections when evaluated as owned values in `expr()`. |
| **8. Typed `copy` and `match` fact tables & fail-closed hooks** (`src/ast.hpp`, `src/semantic_checker.cpp`) | `cfc9d739` (W12) | **REQUIRED** | Provides `TypedCopyFactV1` / `TypedCopyRequirementV1` and `TypedMatchOwnershipFactV1` / `TypedMatchOwnershipRequirementV1` plus fail-closed test hooks (`MOSS_TEST_DROP_COPY_FACT`, `MOSS_TEST_DROP_MATCH_FACT`, etc.). |
| **9. R7-003 / R7-006 / R7-007 write-through borrows, mutable `for` loop lowers, and nested indexed `take(place)`** (`src/semantic_checker.cpp`, `src/interpreter.hpp`, `src/native_generator.cpp`, `src/native_generator_support.hpp`) | `480fa258`, `76af12a6`, `c32bdddd` | **REQUIRED** | Resolves semantic execution for `ORACLE-01`, `ORACLE-04`, `ORACLE-16`, `ORACLE-17`, `ORACLE-18`, `ORACLE-19`, `ORACLE-20`, and `ORACLE-40`. |
| **10. W13 independent consumer & `EnumVariant` recovery matching** (`src/interpreter.hpp`, `examples/swarm/modules/text_toolkit/toolkit/src/stats.moss`) | `31f744fc` (W13 on `phase28-work`) | **REQUIRED** | Preserved intact from `150ab36b`; `examples/swarm/modules/calc_interpreter/repro/natural_full/src/lexer.moss` updated with explicit `copy(chars[i])` to keep `check_phase26_1_c1_corpus_cases.py` green under collection-element consume rejection. |

---

## 4. Residual W12 & Consumer Technical Debt Inventory

1. **Missing v2 `.moss_clone_provenance.json` sidecar emission (`29` oracle fixtures blocked on provenance):**
   - The native generator does not yet emit the v2 `<binary>.moss_clone_provenance.json` sidecar mapping generated Rust `.clone()` lines to `authorized_explicit_copy` (`TypedCopyFactV1`) or `allowlisted_runtime_mechanical` entries. As a result, any accepted oracle fixture that executes natively and contains `.clone()` in generated Rust is classified by `oracle_harness.py` as `BLOCKED_MISSING_OR_INVALID` on the provenance gate even when all 4 semantic/execution gates (`checker`, `fast_debug`, `native_debug`, `native_opt`) pass.
2. **Unauthorized `.clone()` in native lowering of 1-arg `Map.get(key)` and `copy(x)` without `find_copy_fact`:**
   - `src/native_generator.cpp:4861` lowers `copy(x)` by checking `callee == "copy"` rather than resolving `TypedCopyFactV1` (`find_copy_fact`), and `src/native_generator.cpp:4869` lowers 1-arg `Map.get(k)` to `.get(&(k)).cloned().unwrap_or_else(|| std::process::abort())` (cloning the borrowed value instead of borrowing in place).
3. **Nested `take(...)` native lowering & stateful EST traversal in `ownership_take_cursor` / `find_copy`:**
   - `ownership_take_cursor` (`src/semantic_checker.cpp:25556`) and `find_copy` (`src/semantic_checker.cpp:26232`) still use per-statement visited sets (`visited_takes_in_current_stmt_`, `visited_copies_in_current_stmt_`) during recursive EST walks rather than passing `est::ExprId` directly through `check_ownership_expression_walk`, and `src/native_generator.cpp` only lowers `take(place)` at statement root slots (nested `take(...)` inside arbitrary call arguments works in Fast Debug but is not yet lowered by the native Rust emitter).
4. **`CollectionError` standard error enum and collection bounds/missing-key raise semantics (`ORACLE-07`, `48`, `53`, `54`, `55`):**
   - W12 did not register `CollectionError` (`OutOfBounds`, `MissingKey`, `Empty`) in `standard_errors` or attach `CollectionError` raise effects to `Vector.remove`, `Vector.swap_remove`, `Map.remove`, `Map.get(key)`, or `Queue.pop()`.

---

## 5. Integration Gate & Regression Results

### A. Targeted Suites
All targeted suites passed on `phase28-r7-008-integration`:
- `python3 tests/phase28/oracle/test_oracle_harness.py`: **PASS (16/16)**
- `python3 tests/tooling/check_phase28_w12_checker.py ./moss`: **PASS** (both `moss-dev` and `moss-opt`)
- `python3 tests/tooling/check_phase28_r7_003_consumers.py ./moss`: **PASS**
- `python3 tests/tooling/check_phase28_r7_006_checker_facts.py ./moss`: **PASS**
- `python3 tests/tooling/check_phase28_local_ownership.py ./moss`: **PASS**
- `python3 tests/tooling/check_phase28_take_consumer.py ./moss`: **PASS (22/22 fail-closed tests)**
- `python3 tests/tooling/check_phase28_team_b_consumer.py ./moss`: **PASS**
- `python3 tests/tooling/check_phase28_specialized_ownership_contract.py ./moss`: **PASS**
- `python3 tests/tooling/check_phase28_specialized_native_consumer.py ./moss`: **PASS**
- `python3 tests/tooling/check_phase28_selector_argument_effects.py ./moss`: **PASS**
- `python3 tests/phase28/verify_take_fixtures.py`: **PASS (25/25 fixtures)**
- `python3 tests/tooling/check_enum_semantics.py ./moss`: **PASS**
- `python3 tests/tooling/check_phase26_1_c1_corpus_cases.py ./moss`: **PASS**
- `python3 tests/tooling/check_phase20_release_semantics.py ./moss`: **PASS**
- `python3 tests/tooling/check_phase27_string_debt.py`: **PASS (`42` / `7`)**
- `python3 tests/tooling/check_agent_feedback.py`: **PASS**

### B. Full `make check`
- `make --jobs="$(nproc)" check`: **166 of 167 jobs PASS, 1 pre-existing FAIL (`check_phase21_borrowed_view_write`), 0 SKIP (353.72s)** — **ZERO regressions** relative to `phase28-work` (`150ab36b41e45504aaa400aa717da241be7542fb`), which has the exact same 166/167 PASS ledger (`check_phase21_borrowed_view_write` is the known pre-existing Phase 21 cross-crate borrowed view parameter write defect). All 22 regressions inherited from raw W12 (`cfc9d739`) and R7-006 (`76af12a6`) were identified and repaired during reconciliation.

---

## 6. Certified 56-Fixture Oracle Comparison (Baseline vs Integrated Staging)

### A. High-Level Metrics

| Metric | Certified Baseline (`150ab36b` / `54450677`) | Integrated Staging (`phase28-r7-008-integration`) | Delta |
|---|---:|---:|---:|
| **Total Fixtures** | 56 | 56 | 0 |
| **Passed All Applicable Gates (`status == PASS`)** | 8 | **11** | **+3** (`+ORACLE-08`, `+ORACLE-10B`, `+ORACLE-14`) |
| **Semantic Pass (`semantic_status == PASS`)** | 22 | **38** | **+16** (`+ORACLE-01`, `04`, `05`, `06`, `08`, `10A`, `10B`, `14`, `16`, `17`, `18`, `19`, `20`, `26`, `40`, `42`) |
| **Semantic Failures (`semantic_status == FAIL`)** | 34 | **18** | **-16** (nearly cut in half) |
| **Blocked Only by Missing Clone Provenance (`semantic_status == PASS`, `clone_provenance_status == BLOCKED_MISSING_OR_INVALID`)** | 14 | **27** | **+13** (13 newly executing positive fixtures now pass all 4 execution modes and reach the clone provenance gate) |
| **Total Blocked by Missing Clone Provenance (`clone_provenance_status == BLOCKED_MISSING_OR_INVALID`)** | 19 | **29** | **+10** (10 fixtures that previously failed checker/compile now execute native binaries) |

### B. All 11 Fixtures Passing All Applicable Gates
1. `ORACLE-08-USER-COPY-RESOLUTION` (**NEW in R7-008**) — rejects `fn copy` with `RESERVED_KEYWORD`
2. `ORACLE-10B-CLONE-ALIAS-REJECTED` (**NEW in R7-008**) — rejects `.clone()` with `CLONE_ALIAS_UNSUPPORTED`
3. `ORACLE-14-ASSIGN-CONSUME-BORROW-REJECT` (**NEW in R7-008**) — rejects consuming borrowed indexed element with `OWNERSHIP_ILLEGAL_CONSUME`
4. `ORACLE-21-TAKE-LOCAL-REJECTED`
5. `ORACLE-22-TAKE-UNIFIED-FIELD-REJECT`
6. `ORACLE-23-TAKE-VEC-ELEM-REJECTED`
7. `ORACLE-24-TAKE-CUSTOM-SLOT-REJECT`
8. `ORACLE-37-MATCH-CONSUME-UNIFIED-FIELD-REJECT`
9. `ORACLE-41-VEC-FOR-CONSUME-REJECT`
10. `ORACLE-44-HANDLER-PRIMITIVE-MUTATE-REJECT`
11. `ORACLE-52-MUTATE-DURING-ITERATION-REJECTED`

### C. All 13 Newly Passing Semantic/Execution Fixtures (Pass Checker, Fast Debug, Native Debug, and Native `-O`; Blocked Only by v2 Clone Provenance Sidecar)
1. `ORACLE-01-VEC-WRITE-THROUGH` (was `VALUE_MISMATCH` in native debug/opt)
2. `ORACLE-04-MAP-WRITE-THROUGH` (was `VALUE_MISMATCH` in native debug/opt)
3. `ORACLE-05-MAP-GET-BORROW` (was `UNEXPECTED_REJECT` / `CRASH` / `EMIT_FAIL`)
4. `ORACLE-06-MATCH-CONSUME-OPT-NATIVE` (was `UNEXPECTED_REJECT` / `CRASH` / `EMIT_FAIL`)
5. `ORACLE-10A-EXPLICIT-COPY-ONLY` (was `UNEXPECTED_REJECT` / `CRASH` / `EMIT_FAIL`)
6. `ORACLE-16-TAKE-FRONTIER-ARCHIVE` (was `UNEXPECTED_REJECT` / `CRASH` / `EMIT_FAIL`)
7. `ORACLE-17-TAKE-OPTION-FIELD` (was `UNEXPECTED_REJECT` / `CRASH` / `EMIT_FAIL`)
8. `ORACLE-18-TAKE-QUEUE-FIELD` (was `UNEXPECTED_REJECT` / `CRASH` / `EMIT_FAIL`)
9. `ORACLE-19-TAKE-NESTED-VEC-OPTION` (was `UNEXPECTED_REJECT` / `CRASH` / `EMIT_FAIL`)
10. `ORACLE-20-TAKE-NESTED-MAP-VECTOR` (was `UNEXPECTED_REJECT` / `CRASH` / `EMIT_FAIL`)
11. `ORACLE-26-VEC-REMOVE-API` (was `UNEXPECTED_REJECT` / `CRASH` / `EMIT_FAIL`)
12. `ORACLE-40-VEC-FOR-WRITE` (was `RUSTC_FAIL` in native debug/opt)
13. `ORACLE-42-CUSTOM-WRAPPER-REMOVE` (was `UNEXPECTED_REJECT` / `CRASH` / `EMIT_FAIL`)

### D. Breakdown of the 18 Remaining Semantic Failures (Separated from Provenance Blocks)

| Category | Count | Fixtures & Root Cause |
|---|---:|---|
| **1. `CollectionError` Standard Error & Raise Inference Not Yet Implemented** | 5 | `ORACLE-07-CATCHABLE-COLL-ERRORS`, `ORACLE-48-ROOT-MISSING-COLLECTION-ERROR`, `ORACLE-53-MAP-REMOVE-MISSING`, `ORACLE-54-IMPORTED-MOSSI-EFFECTS` (advanced from `provider_mossi=BUILD_FAIL` to consumer checker!), `ORACLE-55-COLLECTION-ERROR-INFERRED-RAISE` — `CollectionError` is not yet registered as a compiler standard error enum or inferred on fallible collection operations. |
| **2. Rejected-Fixture Diagnostic Code / Line-Regex Mismatches (Checker Rejects, Harness Expects Different Code/Regex)** | 5 | `ORACLE-03-COLL-ELEM-CONSUME-REJECT` (checker emits `OWNERSHIP_COLLECTION_ELEMENT_CONSUME` at line 10 `let x = consume_resource(v[0])`, but frozen `diagnostic_mapping.json` has `source_line_regex: "push\|head\\[0\\]"` which only appears in line 1's comment), `ORACLE-28-REBIND-WHILE-BORROWED-REJECT` (emits `OWNERSHIP_SOURCE_BORROWED` vs `OWNERSHIP_USE_AFTER_REBIND`), `ORACLE-30-JOIN-BORROW-FRESH-REJECT` (emits `OWNERSHIP_INCOMPATIBLE_JOIN` vs `OWNERSHIP_MIXED_JOIN`), `ORACLE-34-MATCH-OVERWRITE-WHILE-BORROWED-REJECT` (emits `MATCH_READ_BORROW_ACTIVE` vs `OWNERSHIP_CONFLICTING_ACCESS`), `ORACLE-35-MATCH-ORDINARY-CONSUME-REJECT` (emits `BORROWED_ENUM_PAYLOAD_ACCESS` vs `OWNERSHIP_ILLEGAL_CONSUME`). |
| **3. Match Borrowed Scrutinee Payload Write & Last-Use Liveness** | 2 | `ORACLE-32-MATCH-PAYLOAD-WRITE` (`BORROWED_ENUM_PAYLOAD_MUTATION`), `ORACLE-33-MATCH-LAST-USE-OVERWRITE` (`MATCH_READ_BORROW_ACTIVE` treats borrow as active for entire arm instead of last payload use). |
| **4. Join Promotion, Exceptional Recover Liveness, and Native Borrowed Rebind/Join Lowering** | 4 | `ORACLE-27-REBIND-AFTER-BORROWS` (passes Checker & Fast Debug; native `rustc` `E0308` `&Packet` vs `Packet`), `ORACLE-31-JOIN-PROMOTED-CONSUME` (checker rejects join promotion when branch consumes), `ORACLE-49-OWNERSHIP-AT-RECOVER` (checker marks `a` moved across `try` before `may_fail` raises), `ORACLE-51-JOIN-BORROWED-PROVENANCE-SETS` (passes Checker & Fast Debug; native `rustc` `E0308` `&mut Packet` vs `Packet` at join). |
| **5. Plain Record Field Implicit Consume & Handler Non-Primitive Reply Borrow** | 2 | `ORACLE-15-PLAIN-FIELD-CONSUME-REJECT` (`UNEXPECTED_ACCEPT` on consuming plain non-Copy field without `take`), `ORACLE-46-HANDLER-NONPRIM-REPLY-REJECT` (`UNEXPECTED_ACCEPT` on replying borrowed non-primitive from handler). |
