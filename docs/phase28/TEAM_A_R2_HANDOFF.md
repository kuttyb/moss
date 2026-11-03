# Phase 28 Round 2 — Team A checker handoff

**Branch:** `phase28-r2-team-a`  
**Base:** `phase28-r1-team-a` at `a73d8a6b`  
**Implementation SHA:** `5fbdccdcb48702dffbbbbd98967d3162e7dfa54b`
**Prior handoff documentation checkpoint:** `bbeba8fc53a36485a5a3c038d10ad9dcc3ee890b`
**Status:** bounded checker facts and fixtures delivered; no engine-consumer migration or Phase 28 completion is claimed.

## Typed fact contract

`TypedSemanticModuleV1::local_ownership_facts` publishes
`TypedLocalOwnershipFactV1` records from the checker. Each record carries:

- the exact lexical `definition_identity` and `source_binding_identity`;
- `specialization_context`, the checker statement occurrence, EST arena and
  exact EST expression occurrence;
- typed `Effect` (`Read`, `Write`, or `Consume`), source kind and source/module
  provenance;
- `join_definition_identity` when an existing checker join table identifies
  the predecessor relationship; every reaching definition at that join shares
  its logical join identity;
- `inference_status`, `unsupported_reason`, and `availability`.

`is_intentionally_unsupported()` identifies a bounded inference decline.
`is_authoritative()` is the consumer gate: it requires an inferred record,
`Availability::Present`, non-empty definition/source/context identities, and
valid statement/EST occurrence IDs. Missing EST or
statement occurrence, missing binding identity, and contradictory EST effect
are explicit unavailable states and are not permission to reconstruct or guess
the fact. Select facts by exact definition and specialization context; use the
EST occurrence and source identity to verify the relevant definition. No
consumer changes were made in this handoff.

## Implemented slices

### Existing `if`/`else` live joins

The checker uses its existing `BodyTypeState::joined_bindings` table and exact
predecessor binding IDs. For explicit local definitions in both arms, it scans
the remainder of each reaching arm and the common post-join block to derive
READ, WRITE, or CONSUME demand. Facts remain per definition and share the
checker join identity. Existing ownership validation remains responsible for
availability and use-after-move diagnostics. The slice declines unsupported
nested control flow rather than building an independent control-flow engine.

Executable fixtures cover live READ, WRITE and CONSUME joins, an illegal use
after a joined consume, an alias embedded in a returned owned aggregate, a
copy-valued field return that remains READ, and an explicit unsupported
branch-local case. Non-direct returns that cannot be proven to be a
designated-primitive field projection explicitly decline inference and
preserve the existing ownership path; `option[T]` remains non-Copy.

### Vector iteration

For a Vector `for` loop, the checker publishes a `VectorElement` fact. Its
definition identity is the loop element binding; its source identity is the
checked collection binding; its statement occurrence identifies the loop;
and its EST occurrence is the exact iterable expression. The checker starts at
READ and strengthens to WRITE for checked mutations, including projected
field writes. Element consumption or escape into an owned local is rejected.
Fixtures cover element READ, projected WRITE, an element used only as an
index while another Vector is written, and rejected transfer. The index-use
case remains READ because only the checked place's EST base chain determines
which loop binding is being written.

This starts the per-loop occurrence contract; it does not solve loop-carried
local definitions or iterate ownership requirements to a fixed point.

## E3 Shapes diagnosis

The original `unresolved callable argument` failure was localized and fixed.
During nested leaf-effect capture, method-body inference retained the caller's
semantic context and therefore lacked the implicit-self object needed to resolve
`signed_area()` when passed as a callable argument. The checker now scopes the
captured method owner during this traversal and preserves known callable
identities in the captured environment. The reduced regression
`implicit_method_argument_effect.moss` passes. The Shapes source no longer
reports that error; standalone and package checking now stop earlier at the
separate `MODULE_IDENTITY_UNRESOLVED` error (`module 'shapes' has no package
identity`). Full Shapes package validation therefore remains blocked by module
identity setup, not by the repaired callable-argument path.

## Validation and known boundaries

The first frozen implementation, `37028a7f3f33a6a4d79b9ef3c7b14b777bee47df`,
received CP1 NO-GO for under-classifying non-direct alias returns and marking a
Vector loop element WRITE when it appeared only as an index. Both P1 findings
were fixed at implementation SHA
`73da9d9db0fd35dce195acc1de142994fd756af4`; its re-review found that the
legacy `copy_type` helper treats `option[Int]` as Copy, contrary to the Phase 28
contract. Implementation SHA `5fbdccdcb48702dffbbbbd98967d3162e7dfa54b` admits
only designated primitive field projections, preserves the alias source type
during inference, and adds an option-return rejection fixture. The focused
harness passes in default and shadow modes; exact-SHA Agent 2 re-review is
pending.

Focused validation: `make --jobs="$(nproc)" moss-dev`,
`tests/tooling/check_phase28_local_ownership.py` (18 unique source fixtures)
against `build/compiler/moss-dev` in default and `MOSS_EST=shadow` modes, Python
compilation of the focused harness and parallel runner,
`python3 tests/tooling/check_agent_feedback.py`, and `git diff --check`. The
harness is registered in `make check` for both EST modes.
The Shapes direct check was rerun and returns the module identity diagnostic
above. No full `make check`, `make check-shadow`, native/Fast Debug/optimizer
parity, or corpus suite was run; none is claimed green.

Still open: general CFG liveness and nested/multiway joins; loop fixed points
and loop-carried uses; exceptional edges through `try`/`recover`; imported or
source-free ownership effects; consumer migration and consumer perturb/drop
coverage. `take`, `copy`, enum matching and the wider Phase 28 surface remain
out of scope.

## Team B handoff

Team B can inspect `TypedLocalOwnershipFactV1` and
`TypedSemanticModuleV1::local_ownership_facts` in
[`src/typed_semantic_api.hpp`](../../src/typed_semantic_api.hpp). The ownership
fact vector and exact source fields are checker-published. Consumers must
fail closed for any inferred record whose availability is not `Present`, and
must not derive ownership from expression text. Round 2 fixtures and assertions
are under `tests/phase28/team_a_ownership/` and
`tests/tooling/check_phase28_local_ownership.py`.

**Implementation SHA:** `5fbdccdcb48702dffbbbbd98967d3162e7dfa54b`.
