# Phase 28 Round 2 — Team B Native Consumer Migration & Defect Investigation

**Branch:** `phase28-r2-team-b`
**Role:** Team B, Agent 1 — L6 Coordinator, Coder and Self-Reviewer
**Round 1 Base:** `phase28-r1-team-b` (`83938088`) merged with Team A Round 1 Handoff (`a73d8a6b`)

---

## 1. Executive Summary

In Round 2 (including Fan-in Corrections), Team B:
1. **Integrated Team A's Local Ownership Facts:** Migrated the direct local-alias path in the native code generator (`src/native_generator.cpp`) to consume `TypedLocalOwnershipFactV1` from `TypedSemanticModuleV1::local_ownership_facts` alongside exact EST occurrence facts.
2. **P2 Refactoring (No String Reconstruction):** Refactored `resolve_local_ownership_fact()` to look up facts strictly by retained EST handles (`matched_handle->arena_index`, `matched_handle->expr`) and specialization context. Eliminated all definition identity string formatting and source line number matching.
3. **P1 Genuine Fault Injection:** Removed shortcut throws from the emitter. Implemented genuine fault injection in `Generator::Generator` under test hooks (`MOSS_TEST_CONSUMER_DROP_OWNERSHIP_FACT` and `MOSS_TEST_CONSUMER_PERTURB_OWNERSHIP_FACT`), allowing the production consumer's validation logic to directly catch corrupted availability (`Availability::MissingEstOccurrence`) or contradictory effects against the EST arena and fail closed with `MOSS_INTERNAL_INVARIANT`.
4. **E1 Scalar Temporary Push Repair:** Repaired the scalar Copy argument case of Group A `push_self_field_read` (`nxt.push(head[u])`) by evaluating Copy arguments into an owned scalar temporary before acquiring the mutable receiver borrow. This eliminates Rust E0502 without clones. Separated this from the general non-Copy disjoint-field borrowing defect (`defect_e1_group_a_push_self_field_noncopy.moss`), which remains an open known failure awaiting checker facts.
5. **Enhanced Defect Regression Harness:** Upgraded `tests/phase28/verify_defects.py` to enforce explicit **XPASS** tracking with execution and output parity across 7 targeted fixtures.
6. **Specified E4 Mutable Iteration Interface:** Documented the exact consumer contract required for `ForLoopElementFacts`.

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

### Semantic Authority & Invariant Validation (P2 Compliant)
The consumer enforces semantic authority without string reconstruction or source line matching:
1. **EST Handle Lookup:** Finds candidate fact matching `fact.est_arena_index == matched_handle->arena_index` and `fact.expression_occurrence_id == matched_handle->expr`.
2. **Specialization Context Validation:** Confirms `fact.specialization_context` matches base callable or active specialization context.
3. **Availability Check:** `found_fact->availability == Availability::Present`. Any missing or unavailable state fails closed immediately with `MOSS_INTERNAL_INVARIANT`.
4. **Checker Identity Validation:** Verifies non-empty `definition_identity` and `source_binding_identity`.
5. **EST Arena Parity Check:** Queries `arena->facts(found_fact->expression_occurrence_id, found_fact->specialization_context)->inferred_effect == found_fact->access`. Contradictory effects fail closed immediately with `MOSS_INTERNAL_INVARIANT`.
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
- `MOSS_TEST_DROP_OWNERSHIP_FACT` and `MOSS_TEST_PERTURB_OWNERSHIP_FACT` (checker level) -> fail closed with `MOSS_INTERNAL_INVARIANT`.
- `MOSS_TEST_CONSUMER_DROP_OWNERSHIP_FACT` and `MOSS_TEST_CONSUMER_PERTURB_OWNERSHIP_FACT` (consumer genuine fault injection) -> fail closed with `MOSS_INTERNAL_INVARIANT`.
- Regulated under targeted test `tests/tooling/check_phase28_team_b_consumer.py`.

---

## 3. Investigation & Resolution: E1 Cross-Field Lowering

### The Scalar Temporary Repair (Resolved)
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
- In `MossAccess_Links::link`:
  ```rust
  (*self.__moss_field_nxt_mut()).push((*self.__moss_field_head())[(u) as usize]);
  ```
- Because Rust borrows the receiver `*self` mutably before evaluating method call arguments, `self.__moss_field_nxt_mut()` created an active mutable borrow during the evaluation of `self.__moss_field_head()`, triggering `E0502`.
- **Resolution:** In `native_generator.cpp:gen_block` for `Stmt::Kind::Call`, when `s.b == "push"` and `s.args[0]` has a Copy type (`copy_type(*arg_type) == true`), the generator evaluates the argument into an owned temporary scalar prior to acquiring the receiver's write call place:
  ```rust
  {
      let __moss_push_arg_1 = (*self.__moss_field_head())[(u) as usize];
      (*self.__moss_field_nxt_mut()).push(__moss_push_arg_1);
  }
  ```
- Because `i64` is a Copy type, the immutable borrow of `*self` completes at the temporary binding, leaving `*self` free to be borrowed mutably by `nxt_mut()`.
- Verified: compiles with rustc, runs, and outputs `0` with exact Fast Debug parity.

### The Non-Copy Disjoint Field Borrowing Defect (Open Known Failure)
In `defect_e1_group_a_push_self_field_noncopy.moss`:
- When elements are non-Copy (e.g. `head: Vector[Box]`), evaluating `head[u]` cannot be copied without `.clone()` (which violates the compiler contract).
- Rust rejects moving out of the index (`E0507`) and rejects overlapping whole-receiver borrows (`E0502`).
- Resolving this without `.clone()` requires checker-owned disjoint field projection facts and split-borrow accessors on `MossAccess_*`. Classified separately as an open known failure.

---

## 4. Defect Regression Gate

`tests/phase28/verify_defects.py` enforces expected outcomes across 7 fixtures:
- `E1_group_a_push_self_field_scalar`: **PASS** (Repaired scalar temporary push, output `0`)
- `E1_group_a_push_self_field_noncopy`: **KNOWN FAILURE** (Rust `E0502`/`E0507` on non-Copy disjoint field access)
- `E1_group_a_field_self_assign`: **PASS** (Repaired in Round 1 via RHS-before-LHS temporary binding, output `1`)
- `E2_swarm_035_nested_mutating_calls`: **PASS** (Canonical reproducer passes with parity, output `1`)
- `E3_t6_untyped_for_read`: **PASS** (Standalone read-only loop passes with parity, output `3`)
- `E3_t6_shapes_inference`: **KNOWN FAILURE** (Checker leaf-effect analysis failure: `unresolved callable argument`)
- `E4_mutate_loop_element`: **KNOWN FAILURE** (Rust `E0596`: checker infers `&self`; emitter hardcodes `.iter()`)

```text
=== Summary ===
Total: 7 | PASS: 4 | XPASS: 0 | KNOWN FAILURES: 3 | UNEXPECTED FAILURES: 0
GATE RESULT: PASSED (all expected passes and known failures confirmed)
```

---

## 5. Outstanding Checker Dependencies for Team A

1. **Checker-Owned Fact Applicability / Coverage Record:**
   - To fully prove fail-closed behavior when a required fact is absent (dropped from `local_ownership_facts` entirely), the checker must publish an applicability list (e.g. `required_ownership_definitions` or statement IDs requiring ownership facts). Without this, the consumer cannot distinguish an unsupported statement from a missing required fact without inventing heuristics.
2. **Loop Element Mutation Facts (`ForLoopElementFacts` for E4):**
   - Statement occurrence ID for `Stmt::Kind::For`.
   - Induction element binding identity and access effect (`Effect::Read` vs `Effect::Write`).
   - Collection expression occurrence ID and access effect (`Effect::Read` vs `Effect::Write`).
   - Receiver write effect propagation (inferred `&mut self` when iterating mutably over fields).
3. **Disjoint Field Projection Facts (for Non-Copy E1):**
   - Storage leaf identities and active borrow sets for simultaneous field accesses on receivers across trait methods.
4. **Control-Flow Local Ownership Facts:**
   - Ownership facts across CFG branches and joins (`if`/`match`), allowing consumer borrow lowering beyond straight-line blocks.
