# Phase 22.3 Semantic Query Mapping

Phase 22.3 responds to the measured Phase 22.2 result: teaching diagnostics reduced
failed semantic queries and eliminated `QUERY_TARGET_NOT_FOUND` in the post-22.1 run,
but completion, first-validation success, attempts, and total tool calls did not improve.
The query layer therefore exposes facts already owned by checking, specialization,
call resolution, topology, functional analysis, and synchronization planning.

The design rule is: **ask the compiler for semantic facts before reconstructing them
from source.** Generated Rust is never a semantic input.

## Benchmark-driven capabilities

| Observed benchmark behavior | Phase 22.3 capability | Intended measurable effect |
|---|---|---|
| AB015 needed three effects queries and exact qualified targets to understand a borrowed message payload | `resolve`, structured ambiguity candidates, handler `ownership_facts`, and `domain` payload/reply boundaries | Fewer failed target attempts and fewer separate queries to establish the boundary |
| AB022 repeated check, ownership, and effects calls while repairing overlapping WRITE/READ arguments | Stable diagnostic entity references plus ownership modes and structured why evidence | Fewer diagnostic/query loops; compiler facts can be correlated by semantic ID |
| Agents inspected source to infer which declaration or call occupied a line | `line:`, `at:line:column`, `--kind`, `--enclosing`, and durable candidate IDs | Reduce repeated source inspection and eliminate silent location guesses |
| Agents had to infer specialization or helper dispatch from source | Call-site IDs, callee IDs, concrete argument types, selected specialization, callers, and transitive calls | Obtain resolved dispatch in one compiler query |
| Domain tasks required reconstructing message direction and legal payload behavior | Retained checked message edges plus `domain` sender/receiver, concrete-instance precision, and by-value constraints | Reduce source reconstruction around domain interactions |
| Synchronization/effect tasks required reading broad plans or repeating focused queries | Target-specific projection of the existing `SynchronizationPlan` and scoped direct/transitive effect summaries | Return one relevant footprint without creating a second analysis |
| Optimization tasks depended on human explanation strings | `why.reason.code` and typed evidence, while preserving legacy explanations | Let agents branch on stable fields instead of parsing prose |

These are hypotheses, not claimed improvements. The post-22.3 benchmark report records
the actual before/after metrics.

## Resolution contract

`moss resolve|inspect|type|effects|ownership|calls|why|cost` accepts:

- durable `entity-v1` IDs;
- current-build semantic identities;
- canonical or unambiguous declaration names;
- `line:N` and `at:N:C` locations;
- `<source.moss>:N[:C]` shorthand;
- optional `--kind` and `--enclosing` filters.

The compiler returns `resolved`, `ambiguous`, or `missing`. Ambiguity never chooses a
ranked guess. Candidate order is deterministic by physical source, line, kind, and
durable ID. Current source ranges are line-precision; column input is a narrowing hint,
not invented expression provenance.

## Reused compiler representations

- `SemanticTargetFact` and `entity-v1` finalization provide identity, type, source,
  enclosing entity, aliases, hashes, and specialization facts.
- inferred parameter/receiver effects provide READ/WRITE/CONSUME.
- `ObservableEffects` provides retained callable and functional effects.
- `SemanticCallEdge` provides ordinary and checked synchronous-message edges.
- `ConcreteDomainGraph` provides concrete domain instances and routes.
- `SynchronizationPlan` provides handler footprints, protected reads, classes, ranks,
  acquisition modes, placement, and conservative plan facts.
- teaching diagnostics provide illegal-operation rule/cause/entity/guidance evidence.

Human-facing compatibility fields and JSON fields are serialized from these same
records. No query reparses generated Rust or performs heuristic Moss analysis.

## Deliberate availability limits

- Callable direct effects are not retained separately from the transitive summary;
  `effect_summary.direct_status` is `not_retained` rather than an approximation.
- Per-use move provenance is validated but not retained after checking;
  `ownership_facts.move_provenance` is `not_available`. Rejected use-after-consume
  operations remain available through structured diagnostics.
- Source provenance is line-oriented. Candidate records report that precision.
- A receiver instance is exact only when the checked concrete graph has one matching
  domain instance; otherwise the result explicitly reports ambiguity or unavailability.
- Nested message expressions inside an ordinary expression are validated, but only
  checked `Stmt::Message` invocations currently become semantic message call edges.
  Their effects and topology remain available from the other compiler records.

Trace slicing belongs to Phase 22.4. Phase 22.5 adds the Emacs Semantic IDE;
source repair and workflow automation are reassigned to Phase 22.6.
