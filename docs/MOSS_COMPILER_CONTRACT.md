# Moss compiler contract (DRAFT, rev 2, Nov 1 2026)

The compiler counterpart to the Moss language docs: the architectural invariants every stage must hold. Each invariant names the gate that enforces it and its current status. Where an invariant is not yet met, the gap is recorded as **declared known debt** with its current measurement. Debt may shrink; it must not grow.

Terms:
- **Checker:** the front end that decides Moss semantics.
- **Consumers:** every stage after the checker: Fast Debug, the native emitter, the functional optimizer.
- **Executing and lowering are not deciding.** Computing a + b, or choosing Rust syntax for a checked decision, is allowed. Choosing which + or which function is a decision.

## Invariants

1. **One parse (consumers).** Consumers never parse expression text. Each expression is parsed once, at the initial parse of its source file or the initial load of an imported .mossi body; consumers navigate the retained tree. Diagnostics, fmt and edit tools are exempt.
   - Gate: string-debt gate (`check_phase27_string_debt.py`) and consumer re-parse counter.
   - Status: Fast Debug **met** (zero, enforced by require-zero, Phase 27.5.4 C1). Emitter and optimizer **in progress** (Phase 27.5.4 C2; allowlist ratchet may only shrink).
   - Known debt: the checker's internal string passes and text-keyed parse caches (≈670k re-parses, ≈255M cache lookups on the 27.5.4 corpus measurement). Accepted by decision on Nov 1 2026.

2. **The tree is the source of truth.** Rewrites after parsing should operate on tree nodes; expression text should be derived from the tree, never the reverse.
   - Gate: text-to-tree rebuild counter.
   - Status: **not met.** Known debt: driver rewrites (desugaring, renaming, specialization substitution) still edit text and rebuild trees (≈10k rebuilds on the 27.5.2 corpus). Consumers still receive fresh trees because rebuild-after-rewrite is on by default (27.5.2). Out of scope until the checker/driver redesign.

3. **Nodes are passed, not text (consumers).** Consumers pass node IDs, never look up a node by expression text or text fingerprint, and never consult a text-keyed cache.
   - Gate: consumer text-lookup counter = 0.
   - Status: Fast Debug **met** (C1). Emitter **in progress** (C2; ≈12M lookups on the 27.5.4 corpus at C2 start). Checker text-keyed caches are covered by the debt in invariant 1.

4. **The checker is the only semantic authority.** Only the checker decides: expression types, call targets, specializations, pipeline types, parameter effects and passing, clone/move/borrow, control-flow joins, pattern meaning, lock requirements. Consumers execute or lower those decisions; they never make them.
   - Gate: per decision class, a perturbation test (change the checker's answer; every engine follows or fails closed) and a drop test (remove the answer; every engine fails closed), registered in make check (`check_phase27_5_4_fact_hooks.py`, both EST modes).
   - Status: call targets, specializations, joins, patterns **met** (C1; three fixtures per class, one chosen by the adversarial reviewer). Lock requirements: test pending.
   - Phase 28 Round 2 status: the checker retains typed EST-backed READ/WRITE/CONSUME facts for eligible straight-line locals and explicit `var`/`let` definitions reaching existing `if`/`else` live-join facts, plus READ/WRITE facts for Vector loop elements keyed by the loop statement and exact iterable occurrence. The join slice reuses checker binding predecessor identities and does not construct a second CFG. `is_authoritative()` and `is_intentionally_unsupported()` separate usable decisions, deliberate inference declines, and incomplete/contradictory records. Emitter, Fast Debug and optimizer still do not consume the facts; loop fixed points, exception edges and imported effects remain open. This does not close ownership consumer debt. Known debt (frozen; the gate fails if the set of non-following engines grows): expression types and pipeline types (the emitter still infers them itself); parameter effects/passing and clone/move/borrow (Phase 28). Residual call-edge cases: ambiguous 40, none 3 (27.5.1).
   - Phase 28 Round 3 `take(place)` checker status: the retained EST represents the language operator separately from calls; the checker publishes exact operator/place/statement/specialization/root identities, projection TypeRefs, WRITE authorization, whole-value transfer and the terminal-specific empty replacement. Direct option, Vector, Map and Queue fields and nested record/index projections are covered, including a domain-state field effect path. Facts and requirements fail closed on deletion, changed access/identity, and duplicate records. Native, Fast Debug and optimizer lowering are not implemented by this Team A checker work. Takes must be the root expression of a supported statement value slot; composition inside another expression has no checked fact path yet. This remains part of Phase 28 ownership debt and does not change the known-debt counts above.

5. **Answers live on every node.** The checker should record its answer on every node it types, per specialization context.
   - Gate: coverage counter (report-only).
   - Status: **not met** (≈9% of consumer questions answered on the 27.5.4 corpus). Known debt, tied to invariant 1's checker debt; this is why invariant 4's type classes are debt.

6. **Code created after checking carries answers.** Desugaring, optimizer rewrites and Fast Debug invocations are built as nodes and are either checked or carry checker-consistent answers.
   - Gate: covered by the consumer gates of invariants 1, 3 and 4.
   - Status: Fast Debug met (C1); emitter/optimizer in progress (C2).

7. **Engines agree.** Fast Debug, native and optimized native produce the same observable behavior: output, exit status, failures and trace events.
   - Gate: three-engine check, Fast Debug/native parity, .mossi parity (in make check).
   - Status: output and exit status **met**. Pending: trace-event parity; seeded program generator (after 27.6).

8. **New decisions ship with their gates.** Any new semantic decision class, including those introduced by language features (Graph DSL, ownership/S3) or new backends (C++/GPU), ships in the same change with its checker-side authority and its perturbation and drop tests registered in make check.
   - Gate: AGENTS.md rule plus review.
   - Status: rule recorded; enforcement by review.

## Maintenance

- Kept current like the language docs. Required reading for Moss compiler development (see AGENTS.md); not required for regular Moss development. Fed to cold-start reviewers alongside the language docs.
- Every phase that changes a status or a debt number updates this file in the same delivery.
- CI should verify that every gate named here exists in make check (pending).
