# Phase 28 Team B R7-006 Checker Fact Completion

**Directive:** P28-R7-006 — Checker Fact Completion
**Date:** 2026-11-04
**Base Branch:** `phase28-r7-003-consumers` (`480fa258265a0b6f05db1b4e353a84f6f82805f9`)
**Work Branch:** `phase28-r7-006-checker-facts`
**File Leases Exercised:** `src/semantic_checker.cpp`, `src/typed_semantic_api.hpp` (temporary exclusive lease transferred to Team B), plus consumer files `src/interpreter.hpp`, `src/native_generator.cpp`.

---

## 1. Executive Summary

Under directive P28-R7-006, L7 transferred responsibility for indexed assignment ownership facts and loop fact synchronization to Team B to unblock ORACLE-01 and ORACLE-04 write-through mutations.

All work items have been completed with zero source-string ownership reconstruction, complete fail-closed contract enforcement, and strict three-engine agreement (Fast Debug, Native Debug, Native Optimized).

---

## 2. Implementation Scope

### P28-R7-006-01: Indexed Assignment Ownership
- **Checker Fact Production (`src/semantic_checker.cpp`):**
  - Updated `BodyOwnershipFlowAnalysis` for `Stmt::Kind::Let`, `Var`, and `Assign`. When the RHS expression is an indexing operation (e.g. `b = v[0]`, `e = m["key"]`), `direct_statement_identifier` is absent; the analysis extracts `source_root` from `storage_location(s.b)`.
  - Sets `def.is_borrow = true`, `def.is_owned = false`, and records `source_storage_path` with projection suffix `".[]"`.
  - Propagates target definition demand back to `source_root` definitions.
  - Prevents collection element borrows (`.find(".[]") != string::npos`) from being mistakenly converted to owned transfers in `unused_transfer` and `is_effective_borrow`.
  - In the main checker pass, extracts `source_root` and looks up `source_binding`. For valid READ/WRITE borrows, produces exact `TypedLocalOwnershipFactV1` with:
    - `source_binding_identity = source_binding->second`
    - `access = *inferred` (`Effect::Read` or `Effect::Write`)
    - Exact occurrence identity and binding identity.
  - Validates container mutability: attempts to write-borrow from an immutable container are rejected with `OWNERSHIP_MUTABLE_BORROW_IMMUTABLE`.
  - Rejects implicit ownership consumption:
    - Non-primitive collection element consumption in indexing expressions is rejected with `OWNERSHIP_COLLECTION_ELEMENT_CONSUME` (exercised by ORACLE-03).
    - Attempts to move non-movable borrowed storage places (tracked by `env.borrowed_local_storage_paths` containing `".[]"`) are rejected with `OWNERSHIP_ILLEGAL_CONSUME` (exercised by ORACLE-14).

### P28-R7-006-02: Loop Fact Synchronization
- **Fact & Requirement Parity (`src/semantic_checker.cpp`):**
  - In Vector iteration analysis (line 25171), when iteration element access strengthens from `Effect::Read` to `Effect::Write`, updates `required_local_ownership_occurrences_[tracked->second].access = fact.access;` and synchronizes the EST arena fact.
  - If access transitions to `Effect::Consume`, updates requirement status and availability synchronously.
- **Consumer Enforcement (`src/native_generator.cpp`):**
  - Updated `resolve_vector_element_ownership_fact` to query `find_local_ownership_requirement` and `find_local_ownership_fact(*requirement)` directly.
  - Fail-closed verification: any missing or perturbed requirement or fact triggers `MOSS_INTERNAL_INVARIANT`.

### Fast Debug Place Resolution (`src/interpreter.hpp`)
- When resolving local alias places for `Effect::Read` or `Effect::Write` on indexed assignment statements, resolves the RHS expression tree and invokes `locate_tree_node(*rhs_tree, rhs_tree->root, frame, statement.line, output)`.
- Replaces unhandled string-based `locate()` calls, resolving the place directly in container storage without `missing checked expression node` errors.

---

## 3. Behavioral Engine Matrix

| Fixture / Oracle ID | Description | Fast Debug | Native Debug | Native Opt | Status |
|:---|:---|:---:|:---:|:---:|:---:|
| **ORACLE-01** (`oracle_01_write_through_vector.moss`) | Vector element write-through (`b = v[0]; b.x = 9`) | `9\n9` | `9\n9` | `9\n9` | **PASS** |
| **ORACLE-04** (`oracle_04_write_through_map_entry.moss`) | Map entry write-through (`e = m["key"]; e.x = 9`) | `9\n9` | `9\n9` | `9\n9` | **PASS** |
| **ORACLE-40** (`oracle_40_vector_for_write.moss`) | Mutable Vector iteration (`for x in v { x.n = ... }`) | `11\n21` | `11\n21` | `11\n21` | **PASS** |
| **ORACLE-03** (`oracle_03_collection_element_consume_rejected.moss`) | Rejection of nonprimitive element consumption (`c = v[0]`) | N/A (Rejected) | N/A (Rejected) | N/A (Rejected) | **PASS** (`OWNERSHIP_COLLECTION_ELEMENT_CONSUME`) |
| **ORACLE-14** (`oracle_14_assignment_consume_borrowed_rejected.moss`) | Rejection of consuming borrowed collection element | N/A (Rejected) | N/A (Rejected) | N/A (Rejected) | **PASS** (`OWNERSHIP_ILLEGAL_CONSUME`) |
| **ORACLE-06** (`oracle_06_match_consume_option_native.moss`) | Option match consume | `target` | `target` | `target` | **PASS** |
| **ORACLE-09** (`oracle_09_user_enum_some_none.moss`) | User enum Some/None distinction | `42` | `42` | `42` | **PASS** |
| **ORACLE-02** (`oracle_02_indexed_field_read_no_clone.moss`) | Indexed field read without cloning | `hello` | `hello` | `hello` | **PASS** (0 clones, provisional) |

---

## 4. Verification & Validation Evidence

1. **Targeted P28-R7-006 Suite:**
   - `python3 tests/tooling/check_phase28_r7_006_checker_facts.py ./moss`: **PASS**
   - `python3 tests/tooling/check_phase28_r7_006_checker_facts.py build/compiler/moss-dev`: **PASS**
   - `python3 tests/tooling/check_phase28_r7_006_checker_facts.py build/compiler/moss-opt`: **PASS**
   - Verified fail-closed behavior on ORACLE-01 (dropped fact, dropped requirement, perturbed access) and ORACLE-40.
2. **Consumer Repairs Suite:**
   - `python3 tests/tooling/check_phase28_r7_003_consumers.py ./moss`: **PASS**
   - `python3 tests/tooling/check_phase28_r7_003_consumers.py build/compiler/moss-dev`: **PASS**
   - `python3 tests/tooling/check_phase28_r7_003_consumers.py build/compiler/moss-opt`: **PASS**
3. **Phase 28 Regression Gates:**
   - `python3 tests/tooling/check_phase28_local_ownership.py build/compiler/moss-dev`: **PASS**
   - `python3 tests/tooling/check_phase28_w12_checker.py build/compiler/moss-dev`: **PASS**
   - `python3 tests/tooling/check_phase28_take_consumer.py build/compiler/moss-dev`: **PASS** (22/22)
   - `python3 tests/tooling/check_phase28_team_b_consumer.py build/compiler/moss-dev`: **PASS**
4. **Code Quality & Developer Experience:**
   - `python3 tests/tooling/check_agent_feedback.py`: **PASS**
   - `git diff --check`: **PASS** (zero whitespace or formatting issues)

---

## 5. Architectural Invariants

- **Sole Semantic Authority:** Checker alone determines ownership facts, requirements, and effect classifications. Consumers read exact facts via `TypedSemanticModuleV1`.
- **Zero Heuristics:** No string matching, pattern parsing, or syntax-based ownership inference in consumers.
- **Oracle Integrity:** No modifications made to `phase28-r7-oracle`.
- **Lease Expiration:** Temporary lease of `src/semantic_checker.cpp` and `src/typed_semantic_api.hpp` expires upon handoff.
- **Delivery Boundary:** Branch `phase28-r7-006-checker-facts` pushed to `origin`. STOP after handoff; no merge to `phase28-work` or `main`.
