# Phase 15.15 implementation plan: SWARM-043 and SWARM-044

Status: approved semantic direction; implementation pending. This plan is executable independently of EXPRESS-008 (user-defined indexing).

## Decisions

### SWARM-043: fields on untyped parameters

Reject direct member-field access when the receiver is an untyped parameter (including aliases whose value remains constrained only by that parameter). Do not infer an unnamed field trait, freeze the parameter to the first caller's type, or defer this error until a concrete call. A concrete parameter type permits ordinary field access, subject to existing visibility, ownership, and effect checks.

Untyped member-*method* calls remain valid inferred method requirements. A named trait is an optional explicit method contract. This plan does not remove or expand that existing behavior.

Diagnostic: point to the field access, explain that field access requires a concrete receiver type, and suggest annotating the parameter or exposing the behavior through a method/trait. Preserve the precise physical source in multi-file and JSON diagnostics.

### SWARM-044: indexing on untyped parameters

An untyped parameter used in `items[key]` retains a static indexing requirement. At each concrete call, resolve the container's existing built-in read-index operation, constrain the key, and infer its result. For indexed assignment, resolve an existing write-index operation and check assigned value, WRITE effect, and ownership. The first caller cannot freeze the parameter or change another call's validity. Reuse existing built-in bounds and missing-key behavior; static type checking does not promise key presence or bounds safety.

This work covers current built-in indexable types only. User-defined opt-in syntax and implementation remain EXPRESS-008, documented separately in `docs/PHASE_15_15_INDEXING_PROPOSAL.md`. Do not add arbitrary operator overloading or make `get`/ `set` methods automatically indexable.

## Implementation sequence

1. Follow `AGENTS.md`: load repository-local skills, run `./moss agent bootstrap --json`, verify `moss-0.1`, read `.codex/CURRENT_STATUS.md`, and follow `examples/swarm/SWARM.md`. Preserve unrelated work.
2. Establish baseline probes and exact existing syntax for field access, Vector/Map reads and writes, generic call specialization, native execution, Fast Debug, and structured diagnostics. Inspect the canonical tracker and current test harness before editing.
3. Implement SWARM-043 as a checker rejection at the actual field-expression semantic boundary, including aliases and nested helper contexts as supported. Ensure method calls remain accepted and concrete typed fields retain current behavior. Do not mark fixed until negative and positive regressions pass.
4. Implement SWARM-044 using one resolved semantic indexing operation per concrete specialization. Propagate the selected key/result types and read/write effects to checking, native lowering, Fast Debug, and semantic queries. Do not turn a missing indexing operation into a backend error.
5. Cover order independence and chained generic helpers. Test `Vector[Int]` and `Vector[String]` at separate calls and reversed order, key mismatch, non-indexable type, indexed write type/effect/ownership, nested specialization, multi-file module attribution, and any currently supported Map indexing. Empty-but-well-typed collections remain a runtime bounds/key case under existing semantics. Check a concrete typed field access and an untyped method call as positive controls for SWARM-043.
6. Run focused tests, `make check`, `make examples`, `make all`, `python3 tools/check_swarm_issues.py`, `python3 tools/check_swarm_feedback.py`, and `git diff --check`. Where Fast Debug lacks an operation, report the precise coverage gap rather than claiming parity.
7. Update `docs/LANGUAGE_SYNTAX.md`, the Gentle Introduction, language design, bootstrap/source-surface descriptions as applicable, issue ledger, findings ledger, and `.codex/CURRENT_STATUS.md`. Distinguish implementation done from EXPRESS-008 design pending. Recompute tracker counts with validators. Commit focused changes with clear validation results.

## Required regression matrix

| Case | Expected Moss result |
| --- | --- |
| `fn name(x): return x.name` with untyped `x` | Reject at field expression, independent of call sites |
| Same access with `x: Alpha` | Accept if `Alpha` has accessible field; ordinary checks apply |
| `fn name(x): return x.name()` with untyped `x` | Preserve inferred method requirement and per-call specialization |
| `fn first(xs): return xs[0]`, Vector[Int] and Vector[String] | Accept both orders; distinct result types |
| `first(42)` | Moss diagnostic for unsupported indexing at concrete call |
| `fn at(xs,k): return xs[k]`, valid and invalid key types | Constrain key from concrete container, reject invalid key |
| Indexed assignment through generic helper | Preserve write capability, value type, inferred effect and ownership |
| Valid indexing into empty/missing-key container | Type checks; preserve existing runtime failure semantics |
| Explicit modules / nested generic calls | Correct specialization and physical diagnostic provenance |

## Exit criteria

SWARM-043 and SWARM-044 are independently fixed, with focused regressions and full repository gates passing or a precisely documented environment-only gate limitation. No new user-defined indexing syntax is shipped by this plan. EXPRESS-008 stays open for its opt-in contract checkpoint. SWARM-040 remains in Phase 21.
