# Phase 28 Round 3 — Team B `take(place)` Native & Fast Debug Preparation

**Document:** R03-T02-W00 Technical Design & Lowering Architecture  
**Branch:** `phase28-r3-team-b`  
**Base:** `phase28-r2-integration` at `42b3dc607f2a2468d66df21b068d30e3dffc7946`  
**Role:** Team B, Agent 1 — L6 Coordinator, Implementer and Self-Reviewer  
**Status:** Independent Preparation Complete — Ready for Team A W01 Checker Contract  

---

## 1. Executive Summary

In Round 3 Work Item R03-T02-W00, Team B performed independent consumer preparation for the explicit `take(place)` move operator without waiting idle for Team A and without inventing speculative semantics or parallel analyzers prior to Team A's handoff.

Key accomplishments:
1. **Lowering Mechanisms Identified:** Mapped the closed set of four terminal field types (`option[T]`, `Vector[T]`, `Map[K, V]`, `Queue[T]`) to existing Rust primitives (`std::mem::take(&mut place)`) and Fast Debug interpreter routines (`locate_tree_node`, `std::move(*place.value)`).
2. **Consumer Integration Architecture Specified:** Defined the strict checker-semantic-authority consumer contract, including required typed facts (`TypedTakeFactV1` and `TypedTakeRequirementV1`), fail-closed invariant validation, and zero-clone lowering.
3. **Implementation Hazards Mitigated:** Analyzed single-evaluation of nested projection indices, move-without-clone semantics, and mutation survival across raises (no invented rollback).
4. **Execution Fixtures Created:** Authored 7 minimal, focused execution fixtures in `tests/phase28/take_fixtures/` covering option fields, vector fields, map fields, queue fields, nested projections, partial mutation survival across raises, and illegal rejected forms.
5. **Harness & Regression Gate Built:** Implemented `tests/phase28/verify_take_fixtures.py` asserting returned value ownership, source empty state, repeat take behavior, zero hidden clones, single index evaluation, Fast Debug/native parity, and fail-closed integrity.

---

## 2. Investigation of Existing Lowering & Representations

### 2.1 The Closed Set of Movable Terminal Types (Proposal 5.1)

Per Section 5.1 of the LOL Proposal (`docs/MOSS_PHASE_28_LOL_OWNERSHIP_PEER_REVIEW_PROPOSAL.md`), `take(place)` is strictly restricted to a closed set of four terminal field types:

| Type Family | Moss Type | Rust Type | Language-Defined Empty Replacement State |
| :--- | :--- | :--- | :--- |
| **Option** | `option[T]` | `Option<T>` | `None` |
| **Vector** | `Vector[T]` | `std::vec::Vec<T>` | `[]` (`std::vec::Vec::new()`) |
| **Map** | `Map[K, V]` | `std::collections::HashMap<K, V>` (or `MossMap`) | `{}` (`std::collections::HashMap::new()`) |
| **Queue** | `Queue[T]` | `std::collections::VecDeque<T>` | empty queue (`std::collections::VecDeque::new()`) |

### 2.2 Existing Rust Lowering Mechanisms to Reuse

1. **Uniform Empty State via `std::mem::take`:**
   In Rust, all four target types implement the `std::default::Default` trait:
   - `Option<T>::default() == None`
   - `Vec<T>::default() == Vec::new()`
   - `HashMap<K, V>::default() == HashMap::new()`
   - `VecDeque<T>::default() == VecDeque::new()`
   
   Therefore, standard Rust library function:
   ```rust
   pub fn take<T: Default>(dest: &mut T) -> T
   ```
   can be directly emitted as:
   ```rust
   std::mem::take(&mut <place>)
   ```
   This atomically extracts the existing owned value and installs `Default::default()` in place without reallocating, copying, or duplicating memory.

2. **`MossAccess_*` Mutable Projections:**
   For struct/domain fields accessed via backend access traits (`src/borrowed_views.inc:34`), mutable accessor methods:
   ```rust
   self.__moss_field_<field_name>_mut()
   ```
   return `&mut <FieldType>`, directly providing the mutable place needed for `std::mem::take`.

3. **Place Expressions (`write_call_place`):**
   In `src/native_generator.cpp:1809`, `write_call_place()` resolves mutable lvalues for field members, object projections, and collection index projections.

### 2.3 Existing Fast Debug Lowering Mechanisms to Reuse

1. **Place Resolution via `locate_tree_node`:**
   `src/interpreter.hpp:761`:
   ```cpp
   Place locate_tree_node(const ExpressionTreeView& tree, est::ExprId node,
                          Frame& frame, int line, std::ostream& output);
   ```
   resolves an expression tree node to a `Place { Value* value; DomainValue* domain; string path; }`.
   This already supports:
   - Identifiers: frame locals or domain state.
   - Member access (`est::Member`): object struct fields.
   - Index access (`est::Index`): vector, queue, or map entries.

2. **In-Place Destructive Extraction:**
   Fast Debug can extract the owned value using C++ move semantics:
   ```cpp
   Value extracted = std::move(*place.value);
   ```
   and immediately install the corresponding empty `Value`:
   - `Option`: `Value none = Value::struct_value("option"); none.object->case_name = "None"; *place.value = std::move(none);`
   - `Vector`: `*place.value = Value::vector_value({}, extracted.element_type);`
   - `Map`: `*place.value = Value::map_value();`
   - `Queue`: `*place.value = Value::queue_value();`

3. **State Trace Logging:**
   If `place.domain` is non-null, Fast Debug emits state mutation events:
   ```cpp
   state_event("state_write", frame, line, place, summary(extracted), summary(*place.value));
   ```

### 2.4 Missing Execution Capabilities (Pending Team A Checker Handoff)

1. **Syntax & AST:** The parser does not yet recognize `take(place)` as a built-in unary operator/keyword.
2. **Semantic Checker Contract:** The checker does not yet issue `TypedTakeFactV1` or record `TypedTakeRequirementV1` in `TypedSemanticModuleV1`.
3. **Native Emitter:** No call/operator emission branch exists for `take(place)`.
4. **Fast Debug:** No built-in dispatch exists for `take` in `evaluate_tree_node()`.

---

## 3. Consumer Integration Architecture

### 3.1 Strict Semantic Authority Invariant

Only the checker decides Moss semantics. Emitter and Fast Debug must NEVER:
- Infer whether a target place is movable.
- Guess whether a type belongs to the eligible terminal set.
- Re-parse expression text or match variable name heuristics to decide applicability.
- Silently fall back to a copy/clone when a move fact is missing.

If a `take(...)` occurrence is missing an authoritative checker fact, the consumer MUST fail closed immediately with `MOSS_INTERNAL_INVARIANT`.

### 3.2 Required Typed Facts from Team A

Team B requires Team A to provide the following contracts in `src/typed_semantic_api.hpp` and `TypedSemanticModuleV1`:

```cpp
struct TypedTakeFactV1 {
  enum class TargetPlaceKind {
    FieldProjection,    // e.g. record.field or self.field
    NestedProjection    // e.g. v[i].pending or m[k].items
  };

  enum class TerminalTypeFamily {
    Option,
    Vector,
    Map,
    Queue
  };

  enum class EmptyReplacementState {
    None,
    EmptyVector,
    EmptyMap,
    EmptyQueue
  };

  enum class Availability {
    NotRequired,
    Present,
    MissingEstOccurrence,
    MissingPlaceOccurrence,
    ContradictoryEffect,
    UnsupportedTarget
  };

  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId take_expression_occurrence_id{UINT32_MAX};
  est::ExprId place_expression_occurrence_id{UINT32_MAX};
  std::uint32_t statement_occurrence_id{UINT32_MAX};
  std::string specialization_identity;
  std::string specialization_context;

  TargetPlaceKind target_place_kind{TargetPlaceKind::FieldProjection};
  TerminalTypeFamily terminal_family{TerminalTypeFamily::Option};
  EmptyReplacementState empty_state{EmptyReplacementState::None};
  TypeRef value_type;

  bool is_movable{false};
  bool is_writable{false};
  Availability availability{Availability::MissingEstOccurrence};
  TypedSourceProvenanceV1 provenance;

  bool is_authoritative() const noexcept {
    return availability == Availability::Present &&
           is_movable && is_writable &&
           take_expression_occurrence_id != UINT32_MAX &&
           place_expression_occurrence_id != UINT32_MAX &&
           est_arena_index != UINT32_MAX &&
           !specialization_context.empty();
  }
};

struct TypedTakeRequirementV1 {
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId take_expression_occurrence_id{UINT32_MAX};
  est::ExprId place_expression_occurrence_id{UINT32_MAX};
  std::string specialization_context;
};
```

And corresponding accessors on `TypedSemanticModuleV1`:
- `find_take_fact(arena_index, take_expr_id, specialization_context)`
- `find_take_requirement(arena_index, take_expr_id, specialization_context)`

---

## 4. Implementation Hazards & Mitigations

### 4.1 Hazard 1: Multiple Evaluation of Nested Projections
- **Risk:** In an expression like `take(r.groups[next_index()].tasks[0].pending)`, if the emitter or interpreter evaluates the projection path more than once (e.g. once to read the old value and once to write the empty state), `next_index()` will be executed multiple times.
- **Mitigation:**
  - **Native Lowering:** In Rust, `std::mem::take(&mut <place>)` evaluates `<place>` exactly once as a mutable place reference. Subexpressions (such as index expressions) are evaluated strictly once before the swap occurs.
  - **Fast Debug:** Fast Debug must resolve `locate_tree_node` exactly once to obtain the `Place` pointer, then perform `std::move` and assignment on `*place.value`. It must NOT re-evaluate the AST node.

### 4.2 Hazard 2: Hidden `.clone()` Calls
- **Risk:** Historical compiler implementations frequently inserted `.clone()` on field access or index access when generating Rust.
- **Mitigation:**
  - `take(place)` produces a semantic move directly via `std::mem::take(&mut <place>)`.
  - The verification harness audits generated Rust (`verify_take_fixtures.py`) to assert that no `.clone()` calls are emitted for take lowering.

### 4.3 Hazard 3: Mutation Survival Across Raises (No Invented Rollback)
- **Risk:** If `take(place)` completes and a subsequent expression raises an exception caught by a caller, an engine might erroneously attempt to restore or roll back the emptied field.
- **Mitigation:**
  - Per Phase 21 / Phase 28 specifications, partial state mutations prior to an exception survive across recovery.
  - Fast Debug and Native Emitter must leave the empty state intact upon exception unwinding. No rollback mechanisms are introduced.

### 4.4 Hazard 4: Rejecting Illegal Forms
- **Forms to Reject:**
  1. `take(local)`: Local moves are already inferred; explicit `take` on a local variable is illegal.
  2. `take(v[i])`: Taking an entire collection element is illegal; element removal requires `remove`/`pop`/`swap_remove`.
  3. `take(unified_field)`: Taking a field without a language-defined empty state is illegal.
- **Mitigation:**
  - The checker rejects these with compile-time diagnostics.
  - If a consumer encounters such forms without an authoritative fact, it fails closed.

---

## 5. Test Plan & Fixture Inventory

Seven dedicated fixtures have been authored in `tests/phase28/take_fixtures/`:

| Fixture | Description | Invariants Verified |
| :--- | :--- | :--- |
| `take_option_field.moss` | Option field move-and-empty | Returns `Some(p)`, leaves `None`; repeat take returns `None`; zero clones |
| `take_vector_field.moss` | Vector field move-and-empty | Returns full vector, leaves `[]`; repeat take returns `[]`; zero clones |
| `take_map_field.moss` | Map field move-and-empty | Returns full map, leaves `{}`; repeat take returns `{}`; zero clones |
| `take_queue_field.moss` | Queue field move-and-empty | Returns full queue, leaves empty queue; repeat take returns empty queue |
| `take_nested_projection.moss` | Nested field projections | Projections `v[i].pending` and `m[k].items`; single evaluation of index |
| `take_raise_mutation.moss` | Mutation survival across raises | Emptied field survives typed raise; no invented rollback |
| `take_rejected_forms.moss` | Checker-rejected forms | Asserts rejection of `take(local)`, `take(v[i])`, and `take(unified)` |

The harness `tests/phase28/verify_take_fixtures.py` verifies all 7 fixtures across Fast Debug, Native compilation, Rust clone audit, and execution parity.

---

## 6. Open Questions for Team A Interface Alignment

1. **Option Pattern Matching Parity:** In existing Moss, `match` on legacy `option[T]` is rejected with `MATCH_REQUIRES_ENUM`. Team A's LOL proposal introduces general `option[T]` enum matching (`Some(x)` and `None`). Does Team A plan to deliver general `option[T]` matching simultaneously with `take` in W01?
2. **Domain Synchronization Leaves:** Will `TypedTakeFactV1` directly include domain sync class / lock ordering requirements for domain state targets, or should the consumer query existing domain leaf synchronization facts?
