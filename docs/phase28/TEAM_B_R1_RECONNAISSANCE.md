# Phase 28 Round 1 — Team B Consumer Reconnaissance and Defect Analysis

**Date:** Nov 2, 2026
**Team:** Team B (Consumers and Execution)
**Agent:** Agent 1 — L6 Coordinator, Implementer and Self-Reviewer
**Branch:** `phase28-r1-team-b`
**Baseline:** `phase28-work` at `569d3092`

---

## 1. Executive Summary

In Phase 28 Round 1, Team B performed independent consumer reconnaissance across the native emitter (`src/native_generator.cpp`), Fast Debug (`src/interpreter.hpp`), and the functional optimizer (`src/functional_optimizer.cpp`).

Key deliverables:
1. **Duplication & Clone Census:** Mapped duplicated ownership deductions across consumers, cataloged 28+ hidden `.clone()` sites in the emitter, and identified remaining on-demand expression-string parsing in `native_generator.cpp`.
2. **Defect Regression Gate:** Constructed `tests/phase28/verify_defects.py` as an enforceable regression gate covering defects E1–E4, explicitly distinguishing `PASS`, `KNOWN FAILURE`, and `UNEXPECTED FAILURE`.
3. **Independent Correction for E1 (Group A field self-assign):** Implemented an evaluation-order fix in `src/native_generator.cpp` evaluating RHS before LHS projection in object-view assignments, cleanly resolving Rust `E0502` for this pattern.
4. **Checker-Fact Contract Alignment:** Reviewed Team A's handoff (`docs/phase28/TEAM_A_R1_HANDOFF.md` on `phase28-r1-team-a`) and documented the exact interface boundaries and additional facts needed for Round 2.

---

## 2. Consumer Duplication, Clone, and String Census

### 2.1 Duplicated Ownership Decisions
- **Native Emitter (`src/native_generator.cpp`)**:
  - `function_parameter_abi` / `borrowable_type`: Heuristically inspects AST/EST shapes (`constraint_ops()`) to guess whether an untyped parameter should borrow or pass by value.
  - `borrowed_views.inc`: Implements `borrowed_view_expression()`, `is_object_view()`, and `view_place()` to independently classify expressions as borrowed views rather than reading checked borrow provenance.
  - Vector loop lowering (`native_generator.cpp:6379`): Hardcodes `.iter()` and non-`mut` element bindings without consulting checker loop-element effects.
  - Return values (`native_generator.cpp:5163`): Conditionally clones returned bindings based on `function_effect() != Effect::Consume`.
- **Fast Debug (`src/interpreter.hpp`)**:
  - Dynamic consumed tracking (`interpreter.hpp:3014`): Simulates transfer semantics dynamically at runtime (`frame.consumed.erase()`) rather than executing static reaching definition facts.
  - Full vector copy on iteration (`interpreter.hpp:3357`): Clones `const auto elements = *source;` at loop entry instead of borrowing elements.
  - Value representation shares state dynamically via `std::shared_ptr<StructValue>` and `std::shared_ptr<vector<Value>>`, creating RC-sharing semantics where the native emitter clones.
- **Functional Optimizer (`src/functional_optimizer.cpp`)**:
  - Reads pipeline stage `node.ownership == Effect::Read` (set by the checker) and verifies `node.effects.fusion_safe()`. Pipeline fusion must respect borrow lifetimes and last-use boundaries.

### 2.2 Hidden Clone Census (Emitted Rust `.clone()`)
28+ `.clone()` sites in `src/native_generator.cpp` were cataloged:
- Call arguments: lines 2742, 2797, 7181 (`rendered << "(*" << argument.expression << ").clone()"`)
- Binary operators: lines 3681, 3683 (`return "(" + left + ").clone()"`)
- Member access: line 3797 (`value = "(" + value + ").clone()"`)
- Map indexing: line 3817 (`.expect("Moss missing map key").clone()`)
- Vector indexing: line 3843 (`borrowed_view_expression() ? indexed : "(" + indexed + ").clone()"`)
- Map get fallback: line 4259 (`fallback = "(" + fallback + ").clone()"`)
- Receiver expressions: line 4296 (`(*(" + receiver_expression + ")).clone()`)
- Record field initialization: lines 4693, 7255 (`return "(" + r + ").clone()"`)
- Assignment statements: line 6746 (`assigned = "(" + assigned + ").clone()"`)
- Map insertions: lines 6656, 6662 (`key = "(" + key + ").clone()"`, `inserted = "(" + inserted + ").clone()"`)

### 2.3 Remaining Expression-String Dependencies in Consumers
- `native_generator.cpp`: `expr(...)` takes raw `std::string` rather than `est::ExprId`. Helper functions (`tree_identifier_name`, `tree_strip_expression_grouping`, `tree_generated_binary`, `tree_parse_message`, `tree_parse_member_call`) re-parse strings via `expression_view_from_est()` (`est::parse_view`), contributing to runtime lookup debt.
- `interpreter.hpp`: Reaches retained trees via `expression_tree(...)`, but textual string lookups remain in secondary locations (e.g. `locate(statement.b)`).
- `functional_optimizer.cpp`: `parsed_type(collection_type)` parses type strings.

---

## 3. Analysis of Known Compiler Defects E1–E4

### E1 — Group A (Rust E0502)
- **Status:** Partially repaired (field self-assign PASS; whole-self accessor push remains KNOWN FAILURE).
- **Field Self-Assign (`defect_e1_group_a_field_self_assign.moss`):**
  - Pattern: `pos = moved(pos, d)` inside a method on `Holder`.
  - Cause: Emitter evaluated the mutable LHS place `(*self.__moss_field_pos_mut())` before the RHS expression `moved(pos, d)`. The RHS immutably borrowed `self` while the mutable borrow was already active, violating Rust E0502.
  - Repair: Updated `src/native_generator.cpp:6677-6681` to evaluate the RHS into a temporary block `{ let __moss_assigned = <RHS>; (<LHS>).__moss_replace(__moss_assigned); }` before evaluating the LHS place. This enforces Moss's language evaluation order (RHS before LHS assignment), preserves raise/evaluation semantics, and compiles cleanly with rustc.
- **Push Self-Field Read (`defect_e1_group_a_push_self_field.moss`):**
  - Pattern: `nxt.push(head[u])` in `Links.link(u: Int)`.
  - Cause: Emitter generates `(*self.__moss_field_nxt_mut()).push((*self.__moss_field_head())[(u) as usize])`. Because `MossAccess_*` trait methods take `&mut self` and `&self`, Rust treats the whole object `self` as borrowed mutably.
  - Disposition: Kept open as a **KNOWN FAILURE**. Requires Team A's disjoint projection analysis and emitter projection views.

### E2 — SWARM-035 (Rust E0499)
- **Status:** PASS on canonical fixture `tests/swarm_035_nested_mutating_calls.moss`.
- **Finding:** The canonical reproducer `tests/swarm_035_nested_mutating_calls.moss` compiles cleanly under native and optimized profiles and matches Fast Debug output (`1`). The emitter currently hoists same-receiver nested mutating calls into `let __moss_call_argument_2 = b.take(); b.put(__moss_call_argument_2);` via `call_argument_temp_` (`src/native_generator.cpp:4306`).
- **Disposition:** While the canonical test case passes, generalizing nested call argument sequencing across arbitrary expressions (without relying on special-cased member-call heuristics) requires Team A's checked occurrence ownership facts.

### E3 — T6 (Rust E0596 & Checker Invariant)
- **Status:** PASS on standalone untyped loop; KNOWN FAILURE on complex multi-trait Shapes scenario.
- **Standalone Loop (`defect_e3_t6_untyped_for_read.moss`):**
  - `fn count_items(items): for x in items: ...`: Passes check, Fast Debug (`3`), and native compilation (`3`) with full parity.
- **Complex Shapes Scenario (`examples/swarm/Surface/Shapes/src/main.moss`):**
  - Calling `moss check` on the full Shapes program fails in the checker with:
    `moss: error: internal synchronization invariant: unresolved callable argument`.
  - Investigation: Triggered at `src/semantic_checker.cpp:16107` (`synchronization_require(type.has_value(), "unresolved callable argument")`) when leaf-effect analysis fails to type constructor calls like `Circle(...)` passed as arguments.
  - Classification: Clearly distinguishes **checker inference failure** (`unresolved callable argument`) from the historical **native lowering failure** (lowering untyped parameter specializations to `&mut Vec` on `&self` fields, triggering rustc E0596).

### E4 — mutate_loop_element (Rust E0596)
- **Status:** KNOWN FAILURE.
- **Pattern:** `for it in items: it.take(n)` in `Stock.take_all(n: Int)`.
- **Cause:** Twofold:
  1. The checker infers `Stock.take_all` receiver effect as READ (`&self`), even though elements of `items` are mutated.
  2. The native emitter hardcodes `for <var> in (<collection>).iter()` without `mut`, and fails to take a mutable borrow of `items`.
- **Disposition:** Kept open without emitter-side heuristics. Awaits Team A's `ForLoopElementFacts` providing the checked loop-element WRITE effect.

---

## 4. Defect Regression Gate

`tests/phase28/verify_defects.py` enforces expected outcomes across all six target fixtures:

```text
=== Phase 28 Defect Regression Gate ===

[E1_group_a_push_self_field] Group A push_self_field_read: whole-self field accessor borrowing conflict
  Source: tests/phase28/reproducers/defect_e1_group_a_push_self_field.moss
  Checker: PASS (accepted as expected)
  Fast Debug: PASS (output: '0')
  Native emit: PASS
  Rustc compile: KNOWN FAILURE (failed with expected error: E0502)
  Status: KNOWN FAILURE (Open defect: whole-self field accessors via MossAccess_* trait take &mut self, conflicting with immutable field borrows)

[E1_group_a_field_self_assign] Group A field_self_assign_struct: object replacement with RHS-before-LHS temporary
  Source: tests/phase28/reproducers/defect_e1_group_a_field_self_assign.moss
  Checker: PASS (accepted as expected)
  Fast Debug: PASS (output: '1')
  Native emit: PASS
  Rustc compile: PASS
  Native binary output: '1' (parity verified)
  Status: PASS (Repaired in Round 1 via independent RHS-before-LHS assignment temporary binding in native generator)

[E2_swarm_035_nested_mutating_calls] SWARM-035 canonical reproducer: nested mutating calls on the same receiver
  Source: tests/swarm_035_nested_mutating_calls.moss
  Checker: PASS (accepted as expected)
  Fast Debug: PASS (output: '1')
  Native emit: PASS
  Rustc compile: PASS
  Native binary output: '1' (parity verified)
  Status: PASS (Passes on canonical fixture via call_argument_temp_ in emitter; generalized nested call sequencing awaits checker facts)

[E3_t6_untyped_for_read] T6 untyped read-only loop: vector iteration in standalone function
  Source: tests/phase28/reproducers/defect_e3_t6_untyped_for_read.moss
  Checker: PASS (accepted as expected)
  Fast Debug: PASS (output: '3')
  Native emit: PASS
  Rustc compile: PASS
  Native binary output: '3' (parity verified)
  Status: PASS (Standalone read-only loop passes; complex multi-field Shapes scenario remains a checker inference failure)

[E3_t6_shapes_inference] T6 Shapes complex scenario: multi-trait collection methods trigger checker failure
  Source: examples/swarm/Surface/Shapes/src/main.moss
  Checker: KNOWN FAILURE (rejected with expected marker: unresolved callable argument)
  Note: Open checker defect: synchronization_require fails with 'unresolved callable argument' during leaf-effect analysis

[E4_mutate_loop_element] mutate_loop_element: in-place mutation of vector element within loop
  Source: tests/phase28/reproducers/defect_e4_mutate_loop_element.moss
  Checker: PASS (accepted as expected)
  Fast Debug: PASS (output: '7 17')
  Native emit: PASS
  Rustc compile: KNOWN FAILURE (failed with expected error: E0596)
  Status: KNOWN FAILURE (Open defect: checker infers receiver as READ instead of WRITE; emitter hardcodes .iter() and immutable binding)

=== Summary ===
Total: 6 | PASS: 3 | KNOWN FAILURES: 3 | UNEXPECTED FAILURES: 0
GATE RESULT: PASSED (all expected passes and known failures confirmed)
```

---

## 5. Checker-Fact Contract Alignment (Team A Handoff)

Team A published its Round 1 handoff on branch `phase28-r1-team-a` (`docs/phase28/TEAM_A_R1_HANDOFF.md`, commits `df94b292` / `07145a5a`).

### 5.1 Published Interface
- **Data Structures:** `TypedSemanticModuleV1::local_ownership_facts` containing `TypedLocalOwnershipFactV1` records in `src/typed_semantic_api.hpp`, paired with exact EST occurrence effects in `SemanticFacts::inferred_effect` (`src/est/expression_tree.hpp`).
- **Scope:** Bounded straight-line local alias slice only (local-to-local definitions within a single lexical block).
- **Location:** In `src/typed_semantic_api.hpp` and `src/est/expression_tree.hpp` (NOT `type_ir.hpp` or `checked_body_ir.hpp`).
- **Consumer Obligation:** Consumers must require `Availability::Present` and adopt the checked `access` (`Effect::Read`, `Effect::Write`, `Effect::Consume`). Missing or non-`Present` facts must fail closed (`MOSS_INTERNAL_INVARIANT`).

### 5.2 Additional Checker Facts Needed for Round 2
The straight-line alias slice does not yet support the full consumer migration. The following additional checked facts must be supplied in subsequent rounds:
1. **Loop Element Effects (`ForLoopElementFacts`):**
   - Inferred effect (`Effect::Read` vs `Effect::Write`) on `Stmt::Kind::For` loop bindings to drive emitter selection of `.iter()` vs `(&mut ...).iter_mut()` and `mut` binding declaration.
2. **Disjoint Projection Sets / Leaves (`DisjointStorageLeaves`):**
   - Proved disjoint projection paths on `self` to generate non-conflicting field projection views, resolving Group A `push_self_field_read`.
3. **General Expression & Call Argument Ownership Facts:**
   - Occurrence-level ownership demands across all expression sinks to eliminate the remaining 28+ heuristic `.clone()` calls.
4. **Explicit LOL Operator Facts (`TakeFacts`):**
   - Node facts identifying valid `take(place)` expressions across the closed set of movable terminal field types (`option[T]`, `Vector[T]`, `Map[K,V]`, `Queue[T]`).

No consumer integration of Team A's facts was performed in this pass, preserving the staged round boundary.
