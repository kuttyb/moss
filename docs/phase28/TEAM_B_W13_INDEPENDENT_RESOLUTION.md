# Phase 28 Team B — Independent W13 Resolution Report

**Branch:** `phase28-r6-w13-independent`  
**Base:** `origin/phase28-work` at `2a60693cd4251e85cbda08a2481974882b9eca93`  
**Latest Commit:** `86c72174`  
**Operating Mode:** Autonomous independent execution under L7 unblock directive  

---

## 1. Overview and Scope

Per the L7 directive to proceed independently without waiting idle for Team A's W12 checker handoff, Team B resolved actionable legacy regression fixtures, harnesses, and allowlist debt while preserving original test intent and conforming to approved Phase 28 semantics.

No modifications were made to Team A's checker, typed API, parser, or new W12 fixture files. Zero consumer-side semantic workarounds or test weakenings were introduced.

---

## 2. Work Item 1: Five Legacy `take(local)` Failures (5/5 Resolved)

In Phase 28, `take` is a keyword extraction operator requiring an eligible field place (`record.field`, `vector[i]`, `map[k]`, `queue.field`). Direct `take(local)` is intentionally rejected (`TAKE_REQUIRES_FIELD_PLACE`). Multiple legacy fixtures previously declared user helper functions named `fn take(x: T) -> T: return x` and called `take(local)`.

These fixtures were migrated to conformant helper names (`consume_data`, `consume_item`, `consume_vec`, `consume_string`), preserving the exact test intent of consuming the local value:

1. **`check_phase21_addendum_a_c`**:
   - `tests/phase21_addendum_reachable_arm_consumes.moss`: renamed `fn take` to `fn consume_data`.
   - `tests/phase21_addendum_unreachable_arm_consumes.moss`: renamed `fn take` to `fn consume_data`.
   - **Result:** PASS.
2. **`check_phase21_on_fail_capture`**:
   - `tests/phase21_capture_slot_combined.moss`: renamed `fn take` to `fn consume_data`.
   - `tests/phase21_capture_loop_reinit.moss`: renamed `fn take` to `fn consume_data`.
   - `tests/phase21_capture_loop_consumed.moss`: renamed `fn take` to `fn consume_data`.
   - **Result:** PASS.
3. **`check_phase21_recovery_semantics`**:
   - `tests/phase21_string_method_variant.moss`: renamed `fn take` to `fn consume_string`.
   - `src/interpreter.hpp` (Fast Debug): repaired variant identity filtering in `resolve_exceptional_ownership_facts` so that enum variant matching uses exact variant name (`raised.identity.variant`) rather than generic type match (`raised.identity.type`), ensuring correct state-at-raise local availability across distinct enum error branches.
   - **Result:** PASS.
4. **`check_phase21_recover_reinit`**:
   - `tests/phase21_recover_reinit_positive.moss`: renamed `fn take` to `fn consume_item`.
   - `tests/phase21_recover_reinit_negative.moss`: renamed `fn take` to `fn consume_item`.
   - **Result:** PASS.
5. **`check_builtin_generic_typing`**:
   - `tests/tooling/fixtures/builtin_generic_typing/vector_literal_consume_context.moss`: renamed `fn take` to `fn consume_vec`.
   - `tests/tooling/fixtures/builtin_generic_typing/manifest.json`: updated probe command to match.
   - **Result:** PASS (128/128 probes pass).

---

## 3. Work Item 2: Three Borrowed Loop-Element Failures (3/3 Resolved)

Phase 28 ownership rules enforce that elements yielded by `for x in collection:` iteration are borrowed references (`Effect::Read`), not owned values. Consuming them via `.push(x)` or value-moving sinks without indexed access or explicit transfer is rejected with `OWNERSHIP_VECTOR_ELEMENT_CONSUME`.

1. **`check_phase26_1_c1_corpus_cases`**:
   - `examples/swarm/modules/text_toolkit/toolkit/src/stats.moss`: `smallest` and `largest` were consuming the loop element `best = v` during `for v in values:`. Converted to indexed `while i < (values |> count):` traversal.
   - **Result:** PASS (all 12 diff rows, 2 targets pass).
2. **`check_phase25_team_a_semantics`**:
   - `tests/tooling/check_phase25_team_a_semantics.py`:
     - `moved_collection_term`: updated test fixture from `a = []; b = a; b.push(1)` to `var a = []; a.push(1); b = a; echo b[0]` to retain type unification across assignment under Phase 28 read-borrow assignment lowering.
     - `nested_body_type_scopes`: migrated `copy_ints` from `for value in values: result.push(value)` to indexed `while i < (values |> count):` traversal.
   - **Result:** PASS (all 53 cases pass).
3. **`shard_swarm_cases`**:
   - `tests/swarm_048_callable_argument.moss`: migrated `filter_ints` from `for x in items: out.push(x)` to indexed `while i < (items |> count):` traversal.
   - `tests/branch_confined_open_type.moss`: migrated line 19 `result.push(item)` to `result.push(1)` to retain type confinement testing without illegal loop-borrow consumption.
   - **Result:** PASS (all 45 shard cases pass).

---

## 4. Work Item 3: Two Pre-Existing Phase 21/27 Failures (1 Resolved, 1 Known Debt)

1. **`check_phase27_accepts_rustc` (RESOLVED)**:
   - Root Cause: Conformance ratchet failed because three allowlist cases were previously fixed by Phase 28 improvements:
     - `Group A/field_self_assign_struct`: resolved by Phase 28 ownership.
     - `phase221_object_method_control`: resolved by Phase 28 W08 keyword escaping (`r#box`).
     - `repro_push_loop_reference`: resolved by Phase 28 loop ownership.
   - Fix: Ratcheted `tests/phase27/conformance/accepts_rustc_allowlist.json` by removing the three fixed entries (active seeds ratcheted from 6 to 3).
   - **Result:** PASS (240 accepted standalone roots; native margo test passed; 3 active seeds).
2. **`check_phase21_borrowed_view_write` (CONFIRMED PREEXISTING DEBT)**:
   - Root Cause: Missing checker-side rejection for cross-crate FileIO view parameter write in `Consumer` module importing `Provider.mossi`.
   - Classification: Preexisting Phase 21 known debt predating Phase 28. Under file ownership rules, Team B cannot edit `src/semantic_checker.cpp`. This remains documented known debt.

---

## 5. Work Item 4: Six Remaining Failures Reproduced and Classified

All six remaining failures from the baseline ledger were reproduced, isolated, and classified. Conformance expectations are prepared for Team A's W12 checker handoff:

### 5.1 Two Nested `take(place)` Failures (Awaiting Team A W12)

- **`check_generic_specialization`**:
  - Failing probes: `queue_pop.moss:7` (`echo take(q)`), `vector_pop.moss:6` (`echo take(v)`).
  - Diagnostic: `TAKE_CONTEXT_UNSUPPORTED: take(place) requires a direct initializer, assignment, return, reply, or raise value`.
  - Cause: Fixtures use `take` as a function call in expression position (`echo take(...)`). In Phase 28, `take` is a keyword operator restricted to statement positions in Slices A-D.
  - Handoff Requirement: Team A W12 generalized expression composition for nested `take(place)`.
- **`check_phase25_s1_checker_authority`**:
  - Failing line: `check_phase25_s1_checker_authority.py:211` (`echo take(2)` in `test_fileio_false_positives`).
  - Diagnostic: `TAKE_CONTEXT_UNSUPPORTED: take(place) requires a direct initializer, assignment, return, reply, or raise value`.
  - Cause: Test defines `fn take(save: Int)` and invokes `echo take(2)`. Lexer/parser keyword precedence triggers `TAKE_CONTEXT_UNSUPPORTED`.
  - Handoff Requirement: Fixture migration or Team A W12 expression `take` resolution.

### 5.2 Four `use_after_transfer` Failures (Awaiting Team A W12 / Negative Fixture Reconciliation)

- **Affected Jobs**:
  1. `check_semantic_queries`
  2. `shard_negative`
  3. `shard_phase25_int_payloads`
  4. `shard_phase5_tooling`
- **Single Root Cause**: All four jobs execute `examples/use_after_transfer.moss` and expect it to fail compilation (exit code 1).
- **Behavior Under Phase 28**:
  ```moss
  let original = Payload(text: "owned value")
  let destination = original
  echo "destination:", destination.text
  echo "original:", original.text
  ```
  Under Phase 27, `let destination = original` was an unconditional eager move, rejecting line 12. Under approved Phase 28 per-definition ownership inference, `destination = original` is inferred as a `READ` borrow because `destination` only reads its field and does not consume the payload. Because both `destination` and `original` perform non-interfering shared reads, Phase 28 correctly ACCEPTS this code (exit code 0).
- **Handoff Requirement**: The negative fixture expects eager transfer. It must be updated to demand `CONSUME` (e.g. `return destination`, or explicit `take(original)`) or reconciled with Phase 28 negative specifications after Team A's W12 handoff.

---

## 6. Summary Ledger Reconciliation

| Category | Initial Baseline Failures | W13 Status | Remaining |
| :--- | :--- | :--- | :--- |
| **Legacy `take(local)`** | 5 | 5 Resolved | 0 |
| **Borrowed loop-element** | 3 | 3 Resolved | 0 |
| **Pre-existing Phase 21/27** | 2 | 1 Resolved (`check_phase27_accepts_rustc`) | 1 (`check_phase21_borrowed_view_write` - known checker debt) |
| **Nested `take(place)`** | 2 | 2 Reproduced & Classified | 2 (Awaiting Team A W12) |
| **`use_after_transfer`** | 4 | 4 Reproduced & Classified | 4 (Awaiting Team A W12) |
| **Total Failing Jobs** | **16** | **9 Resolved** | **7 Remaining** |

---

## 7. Quality Invariants Verified

- **String Debt**: 42 raw Stmt NAME comparisons, 7 binding comparisons (meets <= 42/7 threshold).
- **Clones**: Zero hidden nonprimitive clones introduced.
- **Agent Feedback**: Validation passed (`tests/tooling/check_agent_feedback.py`).
- **Semantic Authority**: Checker semantics preserved; Fast Debug and Native emitter read checker facts.
- **No Main Merge**: Isolated to `phase28-r6-w13-independent`.
