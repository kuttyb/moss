# Phase 28 Round 1 — Team A checker handoff

**Branch:** `phase28-r1-team-a`  
**Base:** `origin/phase28-work` at `569d3092`  
**Implementation commit:** `df94b292b86eed6dbcd7048a33cbd3dfc1e97e46`
**Handoff commit:** pending
**Status:** bounded checker slice delivered; not a claim that Phase 28.1/28.2 or engine parity is complete.

## Inspected checker and EST path

- Parsed statements retain exact `est::Handle` roots in `Stmt::expression_handles` (`src/ast.hpp`). Assignment RHS roots use `RhsValue`; `let`/`var` initializers use `Initializer`.
- `est::Arena` owns immutable EST nodes and specialization-keyed `SemanticFacts` (`src/est/expression_tree.hpp`). `SemanticFacts::inferred_effect` already carries typed `Effect` values.
- `Checker::BodyTypeState::BindingFact` and `StatementOccurrence` retain binding identities, assigned identities, statement occurrence IDs, and resolved types (`src/semantic_checker.cpp`). `OwnershipEnv::binding_identities` and `consume_binding` are the current local availability authority.
- `check_ownership_block` is the production ownership path. The Round 1 slice reuses its local binding IDs and EST roots; it does not parse RHS strings or key facts by fingerprints.
- `TypedSemanticModuleV1` (`src/typed_semantic_api.hpp`) publishes the facts for consumers through `Program::typed_semantics`.

## Checker-to-consumer fact contract

`TypedSemanticModuleV1::local_ownership_facts` is a vector of `TypedLocalOwnershipFactV1` records. For a supported local-to-local definition, consumers select the exact fact using its lexical `definition_identity` and `specialization_context`, then verify `availability == Present` before use.

Each fact contains:

- `definition_identity`: checker binding identity for the assignment definition, including a new identity for rebinding.
- `source_binding_identity`: checker identity of the source local (including `parameter:N` when applicable).
- `statement_occurrence_id`: checker-owned statement occurrence ID.
- `est_arena_index` and `expression_occurrence_id`: exact assignment RHS EST occurrence.
- `specialization_context`: context used by the checker for the occurrence.
- `access`: typed `Effect::{Read, Write, Consume}` demanded by the definition's straight-line uses.
- `source_kind`: `LocalBinding` for this slice.
- `provenance`: source file, line, and enclosing callable.
- `availability`: `Present`, or an explicit `MissingEstOccurrence`, `MissingStatementOccurrence`, `MissingBindingIdentity`, or `ContradictoryEffect` status. Missing/contradictory facts are not authorization to guess.

The same `access` is retained as `SemanticFacts::inferred_effect` on the exact RHS EST root and specialization context. This lets Team B adapt using the established EST fact reader while the typed module record supplies the definition/source identity link.

## Supported slice and evidence

For direct non-Copy local-to-local initialization/rebinding in one straight-line lexical block, the checker scans retained EST occurrences of the new alias and combines its checked use requirements. `echo`/`message`/`reply` uses are READ; a projected assignment is WRITE; a return or direct local transfer is CONSUME. Method receiver use reuses the statement's checked `MemberDispatchRequirement`. If the block crosses control flow, uses are ambiguous, or source use overlaps the alias's live interval, this slice declines and preserves the existing transfer behavior.

Focused regression cases are in `tests/phase28/team_a_ownership/`, driven by `tests/tooling/check_phase28_local_ownership.py`:

- READ-only alias: accepted; fact access `Read`.
- Writable alias: accepted; fact access `Write`.
- Consuming local transfer: accepted; fact access `Consume`.
- Use after move: rejected with `OWNERSHIP_USE_AFTER_CONSUME`.
- Consume demand from surviving domain state: rejected with `MOSS_COMPILE_ERROR`.
- Test-only fact drop and perturb: both fail closed with `MOSS_INTERNAL_INVARIANT`.

Targeted validation run: `make --jobs="$(nproc)" moss-dev`; `python3 tests/tooling/check_phase28_local_ownership.py ./moss`; `python3 -m py_compile tests/tooling/check_phase28_local_ownership.py tests/run_parallel.py`; `python3 tests/tooling/check_agent_feedback.py`; `git diff --check`. These are focused results only. `make check`, `make check-shadow`, corpus runs, native execution, Fast Debug parity, and optimizer parity were not run, per Round 1 scope.

## Remaining work and boundaries

- CFG joins and liveness: this pass only sees a straight-line block; definitions whose uses cross `if`/`match` joins remain on existing checking.
- Loops: no fixed-point iteration or loop-carried definition inference.
- Typed raises: no exceptional-edge propagation of local ownership facts; existing Phase 21 raise checking remains authoritative.
- Imported effects: no source-free `.mossi` ownership summary is added; imported/unknown effects remain unavailable to this slice.
- No `take`, `copy`, pattern ownership, collection iteration, or new backend lowering was implemented.
- Team B has not wired consumers to this interface. The frozen downstream ownership/parameter-effect debt is unchanged; this handoff does not claim native/Fast Debug/optimizer behavior follows the new facts.
- Agent 2 adversarial review is scheduled for CP1 after the checker local/CFG foundation, not this Round 1 slice.

## Async Team B handoff

Team B may read `TypedLocalOwnershipFactV1` and `TypedSemanticModuleV1::local_ownership_facts` in `src/typed_semantic_api.hpp` on branch `phase28-r1-team-a`. The facts are exact-EST-backed and checker-owned; a consumer must require `Availability::Present` and use `access` as the semantic answer. Unsupported or unavailable definitions have no authority to infer an effect. No consumer files were modified by Team A. Implementation commit: `df94b292b86eed6dbcd7048a33cbd3dfc1e97e46`. The follow-up handoff commit SHA is recorded in the repository status after publication.
