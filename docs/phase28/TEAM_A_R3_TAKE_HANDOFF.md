# Phase 28 Round 3 — Team A `take(place)` checker handoff

The checker represents `take(place)` as `est::Take`, not `est::Call`. The type
checker owns eligibility, the terminal/result TypeRef and the `builtin:take-place-v1`
target. The ownership checker validates exclusive WRITE access and publishes a
`TypedTakeFactV1` plus its required occurrence in `TypedSemanticModuleV1`.

## Consumer lookup

Use the retained expression occurrence already being lowered and the consumer
context already assigned to that checked specialization:

```cpp
const auto& requirement = typed->find_take_requirement(
    arena_index, take_expr_id, specialization_context);
const auto* fact = typed->find_take_fact(requirement);
// `find_take_requirement` validates the fact too; retaining the explicit
// second lookup is useful when the consumer needs the returned fact pointer.
```

Use the overload that also supplies `specialization_identity` when the
consumer already has the checker-issued stable specialization key. Do not parse
the key or derive it from displayed parameter types. A missing, duplicate,
contradictory, perturbed or unavailable requirement/fact throws
`std::invalid_argument`; consumers must convert this to their existing
fail-closed compiler invariant diagnostic.

`fact->take_occurrence_id` and `fact->place_occurrence_id` name the exact EST
nodes in `fact->est_arena_index`. `projection_path` runs from the root storage
binding to the terminal field. Every projection carries its exact expression
occurrence; index projections also carry the selector EST occurrence. Use those
nodes to lower the place and evaluate each selector once. The checker supplies
`terminal_type`, `result_type`, `required_access`, `transfers_original_value`,
`exclusive_projection_access` and `replacement`; the consumer must not decide
any of them from source spelling or Rust representation.

## Replacement mapping

| Checker value | Required empty state |
| --- | --- |
| `TypedTakeReplacementV1::OptionNone` | `None` |
| `TypedTakeReplacementV1::VectorEmpty` | Empty Vector |
| `TypedTakeReplacementV1::MapEmpty` | Empty Map |
| `TypedTakeReplacementV1::QueueEmpty` | Empty Queue |

The result owns the entire previous terminal field. An option is not unwrapped,
and a collection element is not extracted. This contract introduces no clone,
`Default`, generalized swap or user-call resolution.

## Checked slice and limits

Checker facts cover direct initializer, assignment, return, reply and raise value
slots. Eligible terminal fields are option, Vector, Map and Queue. Projection
paths may include nested record fields and Vector/Map indexing, such as
`take(cells[index].maybe)` and `take(cells[index].items)`. Queue terminal fields
are supported; Queue indexing is not established by this work item. Locals,
collection elements as terminal places, ordinary record/enum terminal fields,
immutable incoming handler payloads and active conflicting READ traversals are
rejected by the checker. Other expression contexts do not yet have an ownership
fact path and are rejected or remain blocked by earlier type inference; they
must not be lowered by a consumer fallback.

The checker records WRITE on the projected place through the existing effect
analysis, including captured domain state-leaf effects. A `take` before a typed
raise remains an ordinary checked mutation; the fact does not assert rollback.
Native, Fast Debug and optimizer lowering are not included in Team A's checker
handoff.

## Targeted authority fixtures

`tests/phase28/team_a_ownership/take_positive.moss` covers all replacement kinds,
nested record and indexed projections, repeated take, a domain state field,
and an ordinary `.take(...)` method call. The same registered local ownership
gate checks invalid local/index/record forms, immutable handler payloads, active
borrow conflicts, take before a typed raise, and deletion, access perturbation,
identity perturbation and duplicate-record fail-closed behavior in default and
EST shadow modes.
