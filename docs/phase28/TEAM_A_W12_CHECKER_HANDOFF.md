# Phase 28 Team A W12 Checker Handoff Report

## 1. Executive Summary

- **Branch:** `phase28-r6-w12-checker`
- **Baseline Commit:** `2a60693cd4251e85cbda08a2481974882b9eca93`
- **Implementation SHA:** `57b65dfca56447dca08fbe7bcbe9b31d4d03da2d` (Independent Agent 2 GO certified)
- **Review Verdict:** **GO** (Independent Agent 2 audit completed with zero findings)
- **Scope:** Completed the four remaining Phase 28 checker features per `docs/MOSS_PHASE_28_LOL_OWNERSHIP_PEER_REVIEW_PROPOSAL.md`:
  1. `copy(x)` — explicit duplication of eligible nonprimitive values; rejection of resources and unsupported types; rejection of source-level `.clone()`.
  2. Nested `take(place)` — support for legal expression contexts with exact EST facts, single evaluation, and unchanged extraction restrictions.
  3. `match consume` — owned enum and `option[T]` destructuring, payload transfer, exhaustive cases, use-after-consume rejection, and interior place rejection.
  4. Collection ownership — complete Vector/Map/Queue borrowing, mutation, iteration, and removal rules (`Vector.remove`, `Vector.swap_remove`, `Map.remove`, `Map.get(key)` 1-arg borrowed lookup; element consume rejection on iteration elements).
- **Sole Semantic Authority Invariant:** Strictly preserved. Checker publishes all typed facts and completeness requirements in `TypedSemanticModuleV1`. Lowering and Fast Debug read these facts and never perform independent ownership inference.
- **String Debt:** Preserved at 42 raw Stmt name comparisons and 7 raw Stmt binding comparisons (zero increase, strictly within <= 42/7 threshold).

---

## 2. Fact Contracts & Typed Semantic APIs

All semantic facts are published on `TypedSemanticModuleV1` in `src/typed_semantic_api.hpp`:

### A. Explicit `copy(x)` Contracts
```cpp
struct TypedCopyFactV1 {
  uint32_t statement_id = 0;
  uint32_t line = 0;
  uint32_t column = 0;
  std::string value_type;
};

struct TypedCopyRequirementV1 {
  uint32_t statement_id = 0;
  uint32_t occurrence_index = 0;
  uint32_t line = 0;
  uint32_t column = 0;
  std::string value_type;
};
```
- **Lookup APIs:** `find_copy_fact(statement_id)` and `find_copy_requirement(statement_id, occurrence_index)`.
- **Eligibility:** Scalar primitives (`Int`, `Float`, `Bool`, `Char`), `String`, `option[T]` (where `T` is copy-eligible), `vector[T]`, `map[K, V]`, `queue[T]`, `set[T]`, and eligible user-defined records and enums whose fields are copy-eligible.
- **Ineligible Types:** `FileIO`, `Range`, `RangeBatch`, `executor`, domain handles, and domain routes. Rejected with `COPY_INELIGIBLE_TYPE`.
- **Arity:** Exactly 1 argument required (`COPY_ARGUMENT_ARITY`).
- **Source `.clone()`:** Rejected with `CLONE_ALIAS_UNSUPPORTED`.

### B. Nested `take(place)` Contracts
- **Legal Contexts:** Function/method arguments (`foo(take(p.f))`), constructors (`Point(x: take(p.f))`), and collection literals (`[take(p.f)]`).
- **Single Evaluation:** Evaluated once at the point of occurrence. Visited take places are tracked within the current statement; duplicate extraction of the same place in a single statement is rejected with `DUPLICATE_TAKE_IN_STATEMENT`.
- **Extraction Restrictions:** Preserved strictly:
  - `take(local)` and `take(v[0])` are rejected with `TAKE_REQUIRES_FIELD_PLACE`.
  - `take(unified_field)` is rejected with `TAKE_INELIGIBLE_TERMINAL`.
  - `echo take(...)`, loop iterables, match scrutinees, and condition contexts are rejected with `TAKE_CONTEXT_UNSUPPORTED`.
- **Facts:** Published as `TypedTakeFactV1` and verified via `TypedTakeRequirementV1`.

### C. `match consume` Contracts
```cpp
enum class TypedMatchScrutineeKindV1 : uint8_t {
  DeclaredEnum = 0,
  Option = 1,
};

struct TypedMatchArmFactV1 {
  std::string case_name;
  std::vector<std::string> payload_binding_names;
  std::vector<std::string> payload_binding_types;
};

struct TypedMatchOwnershipFactV1 {
  uint32_t statement_id = 0;
  uint32_t line = 0;
  uint32_t column = 0;
  bool is_consume = false;
  std::string scrutinee_binding;
  TypedMatchScrutineeKindV1 scrutinee_kind = TypedMatchScrutineeKindV1::DeclaredEnum;
  std::string enum_or_option_type;
  std::vector<TypedMatchArmFactV1> arms;
};

struct TypedMatchOwnershipRequirementV1 {
  uint32_t statement_id = 0;
  uint32_t line = 0;
  uint32_t column = 0;
  bool is_consume = false;
  std::string scrutinee_binding;
};
```
- **Lookup APIs:** `find_match_ownership_fact(statement_id)` and `find_match_ownership_requirement(statement_id)`.
- **Scrutinee Restrictions:** Scrutinee must be an owned local binding. Matching from an interior place (`match consume obj.field:`) is rejected with `MATCH_CONSUME_INTERIOR_PLACE`.
- **Payload Ownership:** When `match consume` is used, the payload bindings introduced in match arms are owned (`Effect::Consume`) and available for transfer or take; subsequent use of the consumed scrutinee is rejected with `OWNERSHIP_USE_AFTER_CONSUME`.
- **Option Support:** `match` and `match consume` natively support `option[T]` with arms `case Some(x):` and `case None:`.

### D. Collection Ownership Contracts
- **`Vector.remove(index)`**: Removes element at `index`, shifts subsequent elements left, and returns the removed owned element.
- **`Vector.swap_remove(index)`**: Swaps element at `index` with the last element, pops and returns the removed owned element in $O(1)$.
- **`Map.remove(key)`**: Removes entry with `key` from map and returns the removed value.
- **`Map.get(key)`**: 1-argument borrowed lookup returning `$value` (`V`) without requiring a default fallback value (integrated in P28-R7-008; the earlier handoff note claiming `option[V]` was corrected to match `src/moss.cpp:673` and `ORACLE-05`). (2-argument `Map.get(key, default)` also returns `V`).
- **Iteration Element Protection:** For-loop elements over vectors (`for item in v:`) are borrowed views. Direct consumption of iteration elements is rejected with `OWNERSHIP_VECTOR_ELEMENT_CONSUME`.

---

## 3. Fail-Closed Test Hooks

Under `-DMOSS_TEST_BUILD=1`, the checker and consumers enforce bidirectional fail-closed validation:
- `MOSS_TEST_DROP_COPY_FACT`: drops a copy fact; compiler aborts with `MOSS_INTERNAL_INVARIANT`.
- `MOSS_TEST_DROP_COPY_REQUIREMENT`: drops a copy requirement; compiler aborts with `MOSS_INTERNAL_INVARIANT`.
- `MOSS_TEST_PERTURB_COPY_FACT`: perturbs value type; compiler aborts with `MOSS_INTERNAL_INVARIANT`.
- `MOSS_TEST_DUPLICATE_COPY_FACT`: injects duplicate fact; compiler aborts with `MOSS_INTERNAL_INVARIANT`.
- `MOSS_TEST_DROP_MATCH_FACT`: drops match fact; compiler aborts with `MOSS_INTERNAL_INVARIANT`.
- `MOSS_TEST_DROP_MATCH_REQUIREMENT`: drops match requirement; compiler aborts with `MOSS_INTERNAL_INVARIANT`.
- `MOSS_TEST_PERTURB_MATCH_FACT`: perturbs scrutinee binding; compiler aborts with `MOSS_INTERNAL_INVARIANT`.
- `MOSS_TEST_DUPLICATE_MATCH_FACT`: injects duplicate fact; compiler aborts with `MOSS_INTERNAL_INVARIANT`.

---

## 4. Verification Evidence

### Targeted Verification Matrix
| Test Suite / Tool | Command | Result |
|---|---|---|
| Phase 28 W12 Checker Suite | `python3 tests/tooling/check_phase28_w12_checker.py` | **PASS** (dev & opt) |
| Enum Semantics Suite | `python3 tests/tooling/check_enum_semantics.py` | **PASS** (30/30 passed) |
| Take Fixtures Parity Harness | `python3 tests/phase28/verify_take_fixtures.py` | **PASS** (18/18 with zero clones) |
| Phase 28 Local Ownership | `python3 tests/tooling/check_phase28_local_ownership.py build/compiler/moss-dev` | **PASS** |
| Take & Selector Consumer Authority | `python3 tests/tooling/check_phase28_take_consumer.py build/compiler/moss-dev` | **PASS** (22/22 fail-closed) |
| Agent Skills Check | `python3 tests/tooling/check_agent_skills.py build/compiler/moss-dev` | **PASS** |
| String Debt Check | `python3 tests/tooling/check_phase27_string_debt.py` | **PASS** (42 name / 7 binding) |
| Agent Feedback Check | `python3 tests/tooling/check_agent_feedback.py` | **PASS** |
| Git Diff Hygiene | `git diff --check` | **PASS** (clean) |

### Regression Suite Reconciliation
The branch is rooted at baseline `2a60693c` (prior to W13 integration into `phase28-work`).
- Default parallel regression suite (`env -u MOSS_EST python3 tests/run_parallel.py --jobs 16`) completed with 151 PASS and 16 FAIL.
- All 16 failing jobs match the known pre-existing baseline ledger:
  - 15 fixture issues resolved independently by Team B in W13 (legacy `take` helpers in fixtures, `use_after_transfer` negative fixture, vector iteration element borrowing, Phase 27 allowlist).
  - 1 pre-existing Phase 21 checker defect (`check_phase21_borrowed_view_write`).
- Zero new regressions or crashes.

---

## 5. Resume and Handoff Instructions for New Agent Team

To resume or integrate this work:
1. **Branch Location:** `origin/phase28-r6-w12-checker` at commit `57b65dfca56447dca08fbe7bcbe9b31d4d03da2d`.
2. **Target for Integration:** Merge `phase28-r6-w12-checker` into `phase28-work` (or rebase/merge with `150ab36b41e45504aaa400aa717da241be7542fb` where W13 is already integrated).
3. **Consumer Migration (W12 Consumers & W14):**
   - Consume `TypedCopyFactV1` in `native_generator.cpp` for native copy lowering.
   - Consume `TypedMatchOwnershipFactV1` in pattern match lowering.
   - Use `Vector.remove`, `Vector.swap_remove`, `Map.remove`, and `Map.get(key)` 1-arg in standard libraries.
4. **Mandatory Constraint:** Do NOT merge directly into `main`. Merges must be authorized by L7.
