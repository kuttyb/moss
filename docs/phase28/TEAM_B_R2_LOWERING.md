# Phase 28 Round 2 — Team B Native Consumer Migration & Defect Investigation

**Branch:** `phase28-r2-team-b`
**Role:** Team B, Agent 1 — L6 Coordinator, Coder and Self-Reviewer
**Round 1 Base:** `phase28-r1-team-b` (`83938088`) merged with Team A Round 1 Handoff (`a73d8a6b`)

---

## 1. Executive Summary

In Round 2, Team B:
1. **Integrated Team A's Local Ownership Facts:** Migrated the direct local-alias path in the native code generator (`src/native_generator.cpp`) to consume `TypedLocalOwnershipFactV1` from `TypedSemanticModuleV1::local_ownership_facts` alongside exact EST occurrence facts.
2. **Enforced Strict Fail-Closed Verification:** Validated definition identity, EST occurrence (arena index + expression occurrence ID + arena fact parity), specialization context, and availability (`Availability::Present`). Ensured missing/perturbed facts trigger `MOSS_INTERNAL_INVARIANT`.
3. **Investigated E1 Cross-Field Lowering:** Confirmed that whole-self trait accessor methods in Rust (`MossAccess_*`) prevent disjoint field borrows across method calls unless either argument evaluation order decouples borrow lifetimes in time or the checker publishes explicit disjoint field projection facts.
4. **Enhanced Defect Regression Harness:** Upgraded `tests/phase28/verify_defects.py` to support explicit **XPASS** tracking with full execution and output parity validation.
5. **Specified E4 Mutable Iteration Interface:** Documented the exact consumer contract required for `ForLoopElementFacts` (loop binding identity, element access mode `READ`/`WRITE`, collection access mode `READ`/`WRITE`, and enclosing receiver write effect propagation).

---

## 2. Consumer Integration: Direct Local-Alias Lowering

### Checker-to-Consumer Path
The native emitter accesses checker ownership facts via `Program::typed_semantics->local_ownership_facts`:
- Each candidate local definition (`Stmt::Kind::Assign`, `Stmt::Kind::Let`, `Stmt::Kind::Var`) queries `resolve_local_ownership_fact(statement, functional_context)`.
- For statements within Team A's straight-line local alias slice, the checker provides:
  - `definition_identity`: e.g. `main@6:statement:alias` or `main@6:statement`
  - `source_binding_identity`: origin binding identity
  - `statement_occurrence_id`: statement occurrence ID
  - `est_arena_index` & `expression_occurrence_id`: exact RHS EST occurrence
  - `specialization_context`: specialization or enclosing context
  - `access`: typed `Effect::Read`, `Effect::Write`, or `Effect::Consume`
  - `availability`: `Present`

### Semantic Authority & Invariant Validation
The consumer enforces semantic authority:
1. **Definition Identity Check:** `found_fact->definition_identity == expected_def_id`.
2. **EST Occurrence Check:** `found_fact->est_arena_index == matched_handle->arena_index` and `found_fact->expression_occurrence_id == matched_handle->expr`.
3. **EST Arena Parity Check:** Queries `arena->facts(found_fact->expression_occurrence_id, found_fact->specialization_context)->inferred_effect == found_fact->access`.
4. **Specialization Context Check:** Matches base callable or active specialization context.
5. **Availability Check:** `found_fact->availability == Availability::Present`. Any missing, perturbed, or contradictory state fails closed immediately with `MOSS_INTERNAL_INVARIANT`.
6. **Unsupported Boundaries:** When no fact exists in `local_ownership_facts`, the definition is outside the Phase 28 straight-line slice and preserved on existing baseline lowering without inventing heuristics.

### Generated Rust Lowering Parity
Based on the validated checked fact:
- **`Effect::Read`:** Lowered as shared borrow reference `let <alias> = &(<origin>);`. Avoids moving non-Copy types and eliminates clones.
- **`Effect::Write`:** Lowered as mutable borrow reference `let mut <alias> = &mut (<origin>);`. Permits in-place field updates through Rust auto-deref.
- **`Effect::Consume`:** Lowered as owned transfer (move) `let mut <alias> = <origin>;`.

### Evidence & Verification
- `tests/phase28/team_a_ownership/read_alias.moss` -> compiles cleanly with rustc, outputs `7\n7` (verified Fast Debug parity).
- `tests/phase28/team_a_ownership/write_alias.moss` -> compiles cleanly with rustc, outputs `9` (verified Fast Debug parity).
- `tests/phase28/team_a_ownership/consume_transfer.moss` -> compiles cleanly with rustc, outputs `7` (verified Fast Debug parity).
- `MOSS_TEST_DROP_OWNERSHIP_FACT` and `MOSS_TEST_PERTURB_OWNERSHIP_FACT` -> both fail closed with `MOSS_INTERNAL_INVARIANT`.
- Regulated under targeted test `tests/tooling/check_phase28_team_b_consumer.py`.

---

## 3. Investigation: E1 Cross-Field Lowering (`push_self_field_read`)

### The Defect
In `defect_e1_group_a_push_self_field.moss`:
```moss
type Links:
  head: Vector[Int]
  nxt: Vector[Int]

  fn link(u: Int) -> Int:
    nxt.push(head[u])
    head[u] = u
    return 0
```
In `MossAccess_Links::link`:
```rust
(*self.__moss_field_nxt_mut()).push((*self.__moss_field_head())[(u) as usize]);
```
Rust emits `E0502: cannot borrow *self as immutable because it is also borrowed as mutable`.

### Analysis & Soundness Boundaries
1. **Receiver-Borrow Precedence:** In standard Rust method-call syntax `recv.meth(arg)`, the receiver is borrowed *before* arguments are evaluated. Calling `self.__moss_field_nxt_mut()` mutably borrows all of `*self`. When evaluating argument `self.__moss_field_head()`, `*self` is already mutably borrowed.
2. **Disjointness in Memory:** `nxt` and `head` are distinct struct fields. In concrete `impl Links`, `self.nxt.push(...)` compiles because Rust understands disjoint struct field borrows. However, across trait methods (`MossAccess_Links`), Rust enforces whole-receiver exclusivity.
3. **Sequential Decoupling vs True Disjointness:**
   - If argument evaluation is hoisted before receiver borrowing (`let __elem = (*self.__moss_field_head())[(u) as usize].clone(); (*self.__moss_field_nxt_mut()).push(__elem);`), the immutable borrow ends before the mutable borrow begins.
   - However, if an argument requires *retaining* a reference across the call, hoisting alone is insufficient without a split-borrow method on the trait (e.g. `fn __moss_project_head_nxt(&mut self) -> (&Head, &mut Nxt)`).
4. **Checker Fact Requirement for Team A:**
   - To lower disjoint field access soundly without guessing, Team B requires checker-owned `CheckedDisjointFieldProjectionFacts`:
     - Disjoint storage leaf paths for receiver accesses in each statement.
     - Specification of simultaneously active field borrow sets on `self`.

---

## 4. Defect Regression Gate & XPASS Handling

`tests/phase28/verify_defects.py` now enforces:
- **PASS:** Feature/bug fix is complete with full native binary execution and Fast Debug output parity.
- **KNOWN FAILURE:** Documented defect fails with exact expected compiler/rustc error code.
- **XPASS:** When a known failure fixture unexpectedly succeeds, the harness does not naively pass; it executes the generated native binary, verifies stdout matches Fast Debug output parity, and marks it as a verified repaired defect (`XPASS`).
- **UNEXPECTED FAILURE:** Any regression, crash, or parity mismatch immediately triggers exit code 1.

Current Status:
```text
=== Summary ===
Total: 6 | PASS: 3 | XPASS: 0 | KNOWN FAILURES: 3 | UNEXPECTED FAILURES: 0
GATE RESULT: PASSED (all expected passes and known failures confirmed)
```

---

## 5. E4 Mutable Iteration Lowering Specification

For `defect_e4_mutate_loop_element.moss`:
```moss
type Stock:
  items: Vector[Item]

  fn take_all(n: Int):
    for it in items:
      it.take(n)
```

### Required Checker Contract from Team A (`ForLoopElementFacts`)
1. **Fact Structure:**
   - `statement_occurrence_id`: AST / EST occurrence of `Stmt::Kind::For`.
   - `element_binding`: Name and binding identity of the induction variable (`it`).
   - `collection_expression_id`: EST occurrence of the collection being iterated (`items`).
   - `element_access`: `Effect::Read` vs `Effect::Write` (inferred from induction variable uses in the loop body).
   - `collection_access`: `Effect::Read` vs `Effect::Write` (must propagate mutation of elements back to a mutable borrow of the collection).
   - `availability`: `Present`.
2. **Consumer Action in Native Emitter:**
   - When `element_access == Effect::Write`:
     - Emit traversal via `(<collection>).iter_mut()`.
     - Emit loop variable as mutable: `for mut <it> in ...`.
     - Enclosing function/method receiver must be declared `&mut self` (driven by the checker's write effect).
   - When `element_access == Effect::Read`:
     - Preserve existing `(<collection>).iter()` traversal (with `.copied()` for Copy types).
