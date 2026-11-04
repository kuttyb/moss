# Phase 28 Team B W08: Consumer Migration Map

This document establishes the authoritative inventory and classification of ownership-sensitive decisions across the Moss native code generator, Fast Debug interpreter, and functional optimizer, per the Phase 28 contract and AGENTS.md semantic authority invariant: **Only the checker decides Moss semantics. Consumers must read the checker's answers and never compute them.**

---

## 1. Inventory & Classification Matrix

| Consumer Subsystem | Decision Class / Location | Checker Authority Status | Classification | Typed Fact API / Identity |
| :--- | :--- | :--- | :--- | :--- |
| **Native Emitter** | Selector argument borrow/mut (`lower_est_selector`) | Present (Slice A) | **Already Consumes** | `lookup_selector_facts()` / `FactContext` |
| **Native Emitter** | `take(place)` lowering (`resolve_take_fact`) | Present (Slice A) | **Already Consumes** | `find_take_requirement()` / `find_take_fact()` |
| **Native Emitter** | Straight-line single local binding (`resolve_local_ownership_fact`) | Present (Slice A/B) | **Already Consumes** | `find_local_ownership_requirement()` / `find_local_ownership_fact()` |
| **Native Emitter** | E1 temporary scalar push repair | Present | **Already Consumes** | Direct local check with exact context |
| **Native Emitter** | Rebinding / killed definitions (`Stmt::Kind::Assign`) | Present (Slice A/B) | **Migrating in W08** | `TypedLocalOwnershipFactV1::killed_definition_identity`, `definition_identity` |
| **Native Emitter** | Branch & match reaching-definition joins (`If`, `Match`) | Present (Slice B) | **Migrating in W08** | `TypedLocalOwnershipFactV1::join_definition_identity`, `access` |
| **Native Emitter** | Rust reserved identifier escaping (`r#box`, etc.) | Checked AST/EST | **Migrating in W08** | `rust_identifier(name)` in `rewrite_est_expression_source` and statement bindings |
| **Native Emitter** | Object view aliasing across local bindings | Present (Slice B) | **Migrating in W08** | Propagation of `view_parameters_` on local aliasing |
| **Fast Debug** | `take(place)` execution | Present (Slice A) | **Already Consumes** | `find_take_requirement()` / `find_take_fact()` |
| **Fast Debug** | Local `Let`/`Var` ownership evaluation | Present (Slice A/B) | **Migrating in W08** | `find_local_ownership_requirement()` / `find_local_ownership_fact()` |
| **Fast Debug** | Local `Assign` / rebinding evaluation | Present (Slice A/B) | **Migrating in W08** | `find_local_ownership_requirement()` / `find_local_ownership_fact()` |
| **Fast Debug** | Consumer test hooks (`MOSS_TEST_CONSUMER_*`) | Present | **Migrating in W08** | Drop, perturb, delete hooks on `local_ownership_facts` |
| **Optimizer** | Pipeline stage ownership (`fusion_safe`) | Present | **Already Consumes** | Stage `node.ownership == Effect::Read` |
| **All Consumers** | Loop-carried ownership & backedges | Present (Slice C) | **Deferred to W09** | `TypedLoopOwnershipFactV1` |
| **All Consumers** | Exceptional state-at-raise & recovery | Present (Slice D) | **Deferred to W10** | `TypedExceptionalOwnershipFactV1` |
| **Optimizer** | Multi-stage pipeline sink fusion | Deferred | **Deferred to W11** | Pipeline stage ownership contracts |
| **Native Emitter** | Non-Copy disjoint struct field borrows | Incomplete (E1 non-Copy) | **Authority Incomplete** | Requires non-Copy disjoint projection facts |

---

## 2. Unmigrated Decision Requirements (W08)

### 2.1 Rebinding and Killed Definitions
- **API**: `p_.typed_semantics->find_local_ownership_requirement(arena_idx, expr_id, context)` & `find_local_ownership_fact(req)`.
- **EST Identity**: Expression occurrence on RHS value slot (`est::ExprSlot::RhsValue`, `argument_index = 0`).
- **Semantics**: When `!ownership_fact->killed_definition_identity.empty()`, the assignment defines a new lexical version and terminates the prior definition. In the native emitter, emit shadowed `let mut <binding> = <assigned>;` in Rust instead of mutating an existing reference binding. In Fast Debug, clear prior definition from `frame.locals` and `frame.aliases`.

### 2.2 Branch and Match Control-Flow Joins
- **API**: `local_ownership_facts` with `!fact.join_definition_identity.empty()`.
- **EST Identity**: Statements in branches defining joined locals.
- **Semantics**: The checker computes converged reaching definitions across if/match branches and assigns `join_definition_identity`. At the join header, the native emitter declares `let mut <binding>;` without nominal type annotation, enabling Rust to infer the exact reference/trait type across both branches. Inside branches, the definition lowers according to `ownership_fact->access`.

### 2.3 Identifier Escaping & Borrow Dereference
- **Semantics**: Identifiers colliding with Rust keywords (e.g. `box`) must be rendered using `rust_identifier(...)`. When taking a borrow of a parameter already held by borrowed reference (`borrowed_function_parameters_`), emit `&(*param)` to prevent `&&T` references.
