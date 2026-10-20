# Phase 25.1 generic checked-body architecture (R0 proposal)

Status: historical R0 proposal. The R0 inventory/evidence received final
independent GO at `9c90573bda8daec5bd4254cc5f6c64c5dd3239e4`; the R1 design is
tracked in `PHASE_25_1_R1_ARCHITECTURE.md`, whose separate review gates R1
compiler edits. This note remains source-path history, not current execution
authority.

## Contract

- ABI-v9 stays the default; ABI-v10 is selected only by
  `MOSS_STAGED_ABI=v10`.
- The provider checker is the only semantic authority. The sealed artifact
  carries typed body, type/requirement summary, callable identity, effects,
  provenance and dependency closure. The v10 importer must reject malformed or
  incomplete facts before ordinary checker projection.
- Consumer specialization, ownership/effect checking and native lowering stay
  on the existing compiler path. No alternate type solver, execution engine,
  runtime, source parser, or language syntax is introduced.
- ABI-v9 keeps its existing legacy body behavior. V10 never executes
  `generic_stmt`, `generic_source`, or reconstructed source-shaped expressions.

## Current path and gaps established from source

`src/checked_body_ir.hpp` owns the checked-body-v1 encoding, canonical
TypeRefs, structural statement/expression nodes, decoder bounds and hash
calculation. Its expression tags include parameters, local reads, literals,
operators, fields, vector/map literals, named calls, nominal construction,
Count and Index. Its statement tags include local declarations, assignment,
field/index assignment, returns, branches, loops and match.

The production provider path in `src/moss.cpp` is not yet a general traversal.
`retain_checked_body_ir()` handles a single return expression by parsing the
AST's source-shaped expression text and recognizes a small set of forms. The
separate `retain_checked_collection_loop_ir()` accepts a rigid collection-loop
shape. Import projection dispatches to separate decoders/projectors for empty
vector, collection loop, H3 and arithmetic bodies. `BodyTypeState` retains
many checked TypeRefs keyed by expression text, but the path does not yet
provide one canonical lexical binding/control-flow graph with all operation,
access and effect facts needed for arbitrary valid generic bodies.

This means R1 must replace the source-shape recognizers with one typed
traversal and reduce projector dispatch to decoding plus validated projection.
The encoder's existing additive checked-body-v1 tags may be extended only
when old readers fail closed and old record bytes/hashes remain unchanged. A
wire-contract version change is not part of this proposal.

## Proposed single source of truth

1. During the normal checker pass, retain each generic statement and
   expression's resolved `TypeRef`, source-independent occurrence identity,
   lexical binding identity, mutability, access mode, selected operation or
   callable identity, effects, and control-flow parent/branch identity.
   Capture facts at the checking decision that establishes them; do not
   re-parse text or infer them later from an operation name.
2. One deterministic body walk consumes only those checker-retained facts and
   creates the checked-body-v1 tree. It assigns dense local ordinals, scopes
   declarations and pattern payloads, emits exact private dependency edges,
   and includes complete operation/requirement/effect summaries. Unsupported
   or absent facts make the callable unavailable in v10.
3. The sealed v10 body and summary share callable identity, semantic body hash,
   type-variable bindings, dependency hashes and provenance. Canonical encoding
   and bounded decoding validate node count/depth, ordinals, lexical scope,
   branch structure, operation identity, type substitution, effects and
   dependency closure.
4. Import validates all records before projecting checked nodes into the
   existing Moss AST/checker. The normal checker rechecks the projected tree
   and the existing specialization/native-lowering path executes it. Projection
   emits a trace tied to the callable identity.
5. Per-callable admission remains: an unsupported export does not block a
   separate supported export, while selecting an unsupported or mutated
   callable fails closed. The v9 importer and default are unchanged.

## Existing path retirement plan

- Keep the checked-body-v1 codec and ABI record validators as the one wire
  layer; add common node tags only when needed by a real R0 row.
- Replace provider helpers that recognize parameter-return, arithmetic,
  empty-vector, nominal, and collection-loop source shapes with the unified
  checked traversal as each equivalent regression corpus passes.
- Replace per-shape import decoder/projection dispatch with one validated
  decoder-to-AST projector; retain compatibility adapters only while they
  decode historical v10 records safely. Remove them after migration evidence
  confirms no older sealed record relies on them.
- Keep `generic_stmt` serialization and parsing solely on the ABI-v9 path.
  V10 import discards it before any generic body selection.

## R0 implementation DAG

1. Finish compiler-emitted export inventory and source-free harness index.
2. Establish the A/B/C/D oracle and record red outcomes for existing
   generic SDK callables.
3. Capture a typed construct/effect/access surface map and classify invalid,
   supported, valid-but-unavailable, unverified and environmental rows.
4. Independent reviewer attacks inventory completeness and the proposed
   single-traversal design at an exact pushed SHA. This completed at R0
   checkpoint `9c90573bda8daec5bd4254cc5f6c64c5dd3239e4`.
5. Resolve P0/P1 findings before any R1 compiler edit. The follow-on R1
   architecture review is recorded with the current R1 design checkpoint.

## R1 and later critical path

R1 totality ledger and checker-retained per-occurrence facts -> unified body walk -> canonical codec/decode and
projection -> source-free positive and hostile-artifact tests -> R1 review.
R2 open results, member/nominal operations and dependency closure -> R3
control-flow, iteration, mutation and effects (including actual
`bytes.from_range`) -> R3 review. Then rerun the complete R0 matrix, add
cross-crate/profile/cache/ABI-migration mutations (R4), run final full
regression and independent review (R5). ABI-v9 default and protected refs are
outside this DAG.

## Known R0 gaps to classify

Initial seeds include `bytes.from_range`, call-independent open empty results,
zero-argument helper chains, generic member constraints, nonempty collection
literals, Map/Queue operations, generic nominal methods, branches and early
returns, match, exception/assert effects, nested helper specialization and
multi-module/provider identity. These are not exclusions; each must receive an
evidence-based language-validity and v10 status in the compatibility matrix.
