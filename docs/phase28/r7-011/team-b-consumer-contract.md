# R7-011 Step 1 Team B consumer interface and removal inventory

Status: proposal pinned to Team B branch `phase28-r7-011-team-b-dynamic-fastdebug` at `0c571343`.
This document proposes no checker decision and assigns no specialization identity.

## Required producer contract

The checker owner must provide the following shared API before consumer wiring can
be completed. IDs are opaque table indexes assigned by the producer; consumers must
not derive them from generated names, parameter types, specialization strings,
callable names, line numbers, or context strings.

- `using SpecId = uint32_t;` plus an explicit invalid/sentinel representation or
  `std::optional<SpecId>`; valid IDs index a program-wide immutable specialization
  record table.
- Each specialization record identifies its semantic callable and owns its
  concrete checked specialization. `FunctionSpecialization` carries its `SpecId`.
  The readable specialization key remains diagnostics-only.
- Every EST `Call` fact distinguishes `Static(SpecId)` from `DynamicCallback`.
  Its exact key is `(caller SpecId, arena index, ExprId)`. Missing, duplicate,
  invalid, or contradictory rows are errors.
- The native-emission occurrence cursor must expose the EST arena/ExprId for the
  call being emitted. Existing `SemanticCallEdge` already carries an arena index
  and expression occurrence, but still also relies on source/context strings and
  has no caller/target SpecId.
- Every interpreter execution frame carries its active `SpecId` (root/test bodies
  need producer-assigned identities too). A call pushes the target SpecId obtained
  from the one call-fact lookup.
- `.mossi` serializes the specialization table and each occurrence call fact.
  References across a provider boundary are `(module identity, SpecId)`; loading
  validates bounds, uniqueness, table ownership, and referenced module identity.

Suggested consumer lookup API (names are provisional):

```cpp
CheckedCallSpec lookup_call_spec(
    const Program&, SpecId caller, uint32_t arena, est::ExprId occurrence);
```

The result is either one `Static(SpecId)` or `DynamicCallback`. No target-name
argument is accepted; consumer call resolution cannot influence selection.

## Consumer removals required by L8

- Delete `checked_call_edge_specialization` in `src/ast.hpp` (line 1100 in the
  pinned branch): name/line matching, `<...>` stripping, and `handler:` rewriting.
- Delete the Fast Debug `Missing && Identifier` callback-selection branch in
  `src/interpreter.hpp` around `specialization_for_call` lines 1867–1907. Dynamic
  callbacks must be represented by the checker fact, not inferred from the EST
  node shape by a consumer.
- Replace `Frame.functional_context` and `Frame.specialization_key` where they
  participate in selecting a callee or looking up specialization-scoped EST facts.
  They may remain diagnostic labels only.
- In `src/native_generator.cpp`, remove the `concrete_specialization(...)` fallback
  for resolved static calls at the pinned call lowering around lines 4784 and 6026.
  A missing call fact is an unconditional invariant failure in production.
- Remove selection/fact lookup based on `specialization_context` and
  `functional_context` strings from the native emitter and interpreter as each
  specialization-scoped fact class is migrated to SpecId. No rename/helper wrapper
  around string comparisons satisfies this requirement.

## Required mutation and regression coverage

Register one semantic-authority test job that runs both Fast Debug and native for
`nest_spec`, `dom_spec`, `meth_spec`, `try_spec`, `cb_spec`, `dead_tail2`, `o15b`,
`field_read`, and `field_consume`; outputs must agree, and the last case must reject
at line 9. In both engines, inject dropped, duplicated, and perturbed call facts
without an environment variable in production builds; each must fail closed.

Also cover: absent caller SpecId, invalid table index, duplicate SpecId record,
wrong module on imported `(module, SpecId)`, missing EST occurrence, duplicated
occurrence, and a target SpecId owned by a different callable. Static target
selection must never invoke `concrete_specialization` after a checker-resolved call.

## Current code locations observed

- `src/ast.hpp`: `FunctionSpecialization` has no SpecId; `SemanticCallEdge` has
  EST occurrence fields but its identity is still source/context strings and
  specialization keys.
- `src/est/expression_tree.hpp`: `SemanticFacts.specialization_result` is a
  string; arena fact keys are `(ExprId, specialization_context string)`.
- `src/interpreter.hpp`: `Frame` has string `functional_context` and
  `specialization_key`; `specialization_for_call` still receives them.
- `src/native_generator.cpp`: static-call resolution still queries the legacy
  helper and falls back to argument-type `concrete_specialization`.
- `src/moss.cpp`: module artifact serialization currently transports readable
  specialization keys; no SpecId table or occurrence-to-SpecId call facts exist.

This work is intentionally blocked from pretending to complete the interface until
Team A publishes producer-owned IDs and Team B can consume that exact table. No
string-derived IDs or consumer-side semantic inference are acceptable substitutes.

## Pinned source counts and additional identity-string sites

At `0c571343`, `rg -c` reports `specialization_context`: native emitter 62,
Fast Debug 50. `functional_context`: native emitter 115, Fast Debug 52. These counts
include both frame/body identity plumbing and lookup/selection uses; the whole
consumer-file count must not be reported as if every occurrence is a specialization
selection. During migration, every remaining use must be reviewed and classified;
L8's required endpoint for specialization-scoped fact selection/lookup is zero.

Additional `<...>` string-splitting sites found by `rg` (besides the legacy matcher
at `ast.hpp:1110`) are `interpreter.hpp:4382,4549`, and
`native_generator.cpp:1317,1361,2565,9842,11057`. These need individual review
against the L8 restriction: sites used to choose specialization, query facts, or
classify an active specialization must be replaced by SpecId/table data. Any display
or diagnostics-only use must be demonstrated as such and must not feed selection.

The static emitter selection paths are at `native_generator.cpp:4734,4769,4784`
and `:6026`; the latter two are the explicit concrete-specialization fallbacks.
Fast Debug call selection is at `interpreter.hpp:1854` and `:1932`; its
`Missing && Identifier` fallback begins at `:1867`.

### `.mossi` plumbing boundary

The pinned branch uses a sealed ABI-v10 module interface (`moss-module-interface-v3`)
with semantic Section 1 and versioned executable-body Section 2. The execution-body
loader binds Section 2 to the sealed Section 1 semantic digest. The current API has no
SpecId records or occurrence call facts in either section. Therefore implementation
cannot safely be isolated to just the reader or writer: producer record shape,
assignment order, dense-table validation, imported-module qualification, and the
Section 1 digest contract must be decided together. Once Team A supplies the table and
fact records, Team B can add symmetric writer/reader records and corruption tests
without deriving identity from names or strings. Do not advance the ABI version or
weaken sealed-interface admission as a workaround.
