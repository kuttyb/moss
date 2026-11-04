# Phase 28 R7-003 Consumer Repairs Handoff Report

## Executive Summary

- **Task**: P28-R7-003 Consumer Repairs (Revised Directive)
- **Base Commit**: `31276eb16991561f969eea646b65c29a37c0bb39` (suspended W12 branch `phase28-r6-w12-checker`)
- **Branch**: `phase28-r7-003-consumers`
- **Exclusive Lease**: `src/native_generator.cpp`, `src/native_generator_support.hpp`, `src/interpreter.hpp`, and consumer-specific tests (`tests/phase28/consumer_repair/`, `tests/tooling/check_phase28_r7_003_consumers.py`). No edits to `src/semantic_checker.cpp`.
- **Status**: Complete & Ready for Handoff.

## Mandatory Invariant Adherence

Every ownership-sensitive consumer decision is governed strictly by exact checker-issued facts:
1. `TypedMatchOwnershipFactV1` governs match scrutinee destructuring (`scrutinee_kind == Option` vs `Enum`, `consume_scrutinee` move vs borrow).
2. `TypedLocalOwnershipFactV1` governs local ownership and vector iteration (`SourceKind::VectorElement` with `Effect::Write` -> `.iter_mut()`, induction parameter reborrowing as `&mut (*item)`).
3. `read_call_place` / `write_call_place` for container and indexed projections, eliminating unauthorized struct clones.
4. If a fact is missing from the checker (as in ORACLE-01 and ORACLE-04 for indexed assignment sources), the consumer code is implemented to consume `Effect::Write` facts when issued, and does **not** employ syntax heuristics or string parsing. The missing checker contracts are documented below for Team A.

---

## Work Item Results & Behavioral Assertions

| Work Item | Oracle ID | Fast Debug | Native Debug | Native Opt | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Option match consume** | `ORACLE-06` | `target` (PASS) | `target` (PASS) | `target` (PASS) | **GREEN** |
| **User enum Some/None** | `ORACLE-09` | `42` (PASS) | `42` (PASS) | `42` (PASS) | **GREEN** |
| **Mutable Vector iteration** | `ORACLE-40` | `11\n21` (PASS) | `11\n21` (PASS) | `11\n21` (PASS) | **GREEN** |
| **Indexed field reads** | `ORACLE-02` | `hello` (PASS) | `hello` (PASS) | `hello` (PASS) | **GREEN** (Provisional; 0 struct clones) |
| **Vector write-through** | `ORACLE-01` | Pre-existing | Pre-existing | Pre-existing | **BLOCKED on Checker Fact** (Consumer Ready) |
| **Map write-through** | `ORACLE-04` | Pre-existing | Pre-existing | Pre-existing | **BLOCKED on Checker Fact** (Consumer Ready) |

*Note: `Map.get` and `copy`-name resolution remain deferred per directive.*

---

## Detailed Consumer Repairs

### 1. ORACLE-06: Option match consume (`P28-R7-003-04`)
- **Issue**: Native generator previously emitted `option::Some { value: b }` which failed rustc compilation with `E0433: cannot find module or crate option`.
- **Resolution**: Queried `resolve_match_ownership_fact` for `match_fact->scrutinee_kind == TypedMatchScrutineeKindV1::Option`. Emitted standard Rust `Some(b)` / `None` patterns and honored `s.b == "consume"` by moving the option scrutinee without reference borrowing, preserving owned payload transfers.

### 2. ORACLE-09: User enum Some/None (`P28-R7-003-05`)
- **Issue**: Fast Debug interpreter unconditionally treated any enum case named `"Some"` or `"None"` as the built-in option, causing runtime errors (`missing enum payload`) when user enums declared variants named `Some`/`None`.
- **Resolution**: Used `resolve_match_ownership_fact` in `src/interpreter.hpp` to query `match_fact->scrutinee_kind`. Only built-in options use `option_enum`; user-defined enums resolve their declared type and bind fields using declared field names.

### 3. ORACLE-40: Mutable Vector iteration (`P28-R7-003-06`)
- **Issue**: Vector element iteration was previously lowered as immutable `.iter()`, preventing in-place mutations.
- **Resolution**:
  - Authoritative loop element facts are issued by the checker in `program_.typed_semantics->local_ownership_facts` as `TypedLocalOwnershipFactV1` with `SourceKind::VectorElement`.
  - When `fact.access == Effect::Write`, native generator emits `.iter_mut()`.
  - Registered the loop induction variable in `write_through_parameters_` during loop body emission so that downstream helper calls and operations correctly reborrow the element as `&mut (*item)`.

### 4. ORACLE-02: Indexed field reads (`P28-R7-003-02`)
- **Issue**: Projections on indexed expressions (e.g. `v[0].tag`) called `expr("v[0]")`, which produced a cloned temporary `Data` struct and vector.
- **Resolution**: In `tree_parse_member_access`, when the base expression is an index expression, native generator now calls `read_call_place(projection_base)`. Emits `((v)[(0_i64) as usize]).tag` directly without any clone of `Data`.

---

## Required Checker Contracts for Team A

Per the mandatory invariant, Team B did not introduce heuristic workarounds for missing checker facts. The following checker contracts are required from Team A:

1. **Indexed Assignment Sources (`ORACLE-01` & `ORACLE-04`)**:
   - In statements like `b = v[0]` and `e = m["key"]`, the RHS `s.b` is an indexed place (`est::ExprKind::Index`), not a simple identifier (`direct_statement_identifier`).
   - `src/semantic_checker.cpp` currently only calls `retain_local_ownership_fact` when `local_source != nullptr`. As a result, no `TypedLocalOwnershipFactV1` is issued for local variables bound to indexed expressions.
   - **Contract Required**: The checker must issue a `TypedLocalOwnershipFactV1` for `Stmt::Kind::Assign` / `Var` where the RHS is an indexed place (`v[0]`, `m["key"]`), indicating whether the borrow requires `Effect::Read` or `Effect::Write` (when mutated, e.g., `b.cost = 9`).
   - **Consumer Status**: The native generator is already wired: once `TypedLocalOwnershipFactV1` with `Effect::Write` is issued, `write_call_place` emits `let mut b = &mut ((v)[(0_i64) as usize]);` and `let mut e = &mut (*(m).get_mut(...).expect(...));`, producing `9\n9`.

2. **Checker Fact Requirement Update Discrepancy (`ORACLE-40`)**:
   - In `src/semantic_checker.cpp`, when a vector loop induction variable's mutation is observed, `local_ownership_facts_[tracked->second].access` is escalated to `Effect::Write`. However, `required_local_ownership_occurrences_[tracked->second].access` remains `Effect::Read`.
   - **Contract Required**: When escalating `access` in `local_ownership_facts_`, Team A must also synchronize `required_local_ownership_occurrences_` so that `find_local_ownership_fact(req)` matches without throwing an invariant violation.

---

## Verification & Test Results

1. **`tests/tooling/check_phase28_r7_003_consumers.py`**:
   - `moss` (default): **PASS** (ORACLE-06, ORACLE-09, ORACLE-40, ORACLE-02 all pass in Fast Debug, Native Debug, and Native Optimized).
   - `build/compiler/moss-dev`: **PASS** (All pass).
   - `build/compiler/moss-opt`: **PASS** (All pass).
2. **`tests/tooling/check_phase28_team_b_consumer.py ./moss`**: **PASS** (Zero regressions).
3. **`tests/tooling/check_phase28_w12_checker.py ./moss`**: **PASS** (Zero regressions).
4. **`tests/tooling/check_agent_feedback.py`**: **PASS**.
5. **`git diff --check`**: **PASS** (clean diff hygiene).
