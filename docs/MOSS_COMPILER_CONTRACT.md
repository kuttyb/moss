# Moss compiler contract (rev 4, Phase 27.5.5)

The compiler counterpart to the Moss language docs: the architectural invariants every stage must hold. Each invariant names the gate that enforces it and its current status. Where an invariant is not yet met, the gap is recorded as **declared known debt** with its current measurement. Debt may shrink; it must not grow.

Terms:
- **Checker:** the front end that decides Moss semantics.
- **Consumers:** every stage after the checker: Fast Debug, the native emitter, the functional optimizer.
- **Executing and lowering are not deciding.** Computing a + b, or choosing Rust syntax for a checked decision, is allowed. Choosing which + or which function is a decision.

## Invariants

1. **One parse (consumers).** Consumers never parse expression text. Each expression is parsed once, at the initial parse of its source file or the initial load of an imported .mossi body; consumers navigate the retained tree. Diagnostics, fmt and edit tools are exempt.
   - Gate: string-debt gate (`check_phase27_string_debt.py`) plus the stage counter ratchet (`check_phase27_5_4_runtime_ratchet.py`). `make check` runs default mode; `make check-shadow` runs the separate cross-check mode.
   - Status after Phase 27.5.5: Fast Debug, emitter and optimizer **met** for consumer parsing. The final paired emission sweep covers 1,828 tracked sources in default and `-O`; consumer type-inference requests and consumer reparses are expected to be zero. Checker parsing is intentionally still string-based and is outside the consumer target.
   - Measured debt on the fixed workload: checker 54,037 expression reparses and 16,261,580 text-cache lookups; stage limits are committed in `tests/tooling/fixtures/phase27_5_4_runtime_baseline.json` and cannot rise. The workload includes calculator, regex and string-concatenation programs.

2. **The tree is the source of truth.** Rewrites after parsing should operate on tree nodes; expression text should be derived from the tree, never the reverse.
   - Gate: text-to-tree rebuild counter in the runtime ratchet.
   - Status: **not met.** Driver rewrites still edit text and rebuild expression trees. The fixed workload records 748 driver rebuilds; the committed baseline prevents increases. The C2 retained-tree consumer migration does not change the driver's rewrite representation.

3. **Nodes are passed, not text (consumers).** Consumers pass node IDs, never look up a node by expression text or text fingerprint, and never consult a text-keyed cache.
   - Gate: stage text-lookup counters in the runtime ratchet; consumer stages must remain zero.
   - Status after Phase 27.5.5: Fast Debug, emitter and optimizer **met** (zero text-node lookups on the fixed workload; the final 1,828-source emitter sweep also checks both default and `-O`). The checker still consults text-keyed caches; its 16,261,580 lookups remain measured under invariant 1 and ratcheted.

4. **The checker is the only semantic authority.** Only the checker decides: expression types, call targets, specializations, pipeline types, parameter effects and passing, clone/move/borrow, control-flow joins, pattern meaning, lock requirements. Consumers execute or lower those decisions; they never make them.
   - Gate: per decision class, a perturbation test (change the checker's answer; every engine follows or fails closed) and a drop test (remove the answer; every engine fails closed), registered in make check (`check_phase27_5_4_fact_hooks.py`, both EST modes).
   - Status: expression types and pipeline-stage/output types **met** in Fast Debug, native emission and `-O`; call targets, specializations, joins and patterns remain covered by the C1 perturb/drop tests. The Phase 27.5.5 report records additional unseen reviewer-selected type fixtures and their outcomes. Lock requirements: test pending.
   - Known debt (frozen; the gate fails if the set of non-following engines grows): parameter effects/passing and clone/move/borrow (Phase 27.6; 10 ownership entries). There are no expression-type or pipeline-type entries in the debt JSON. Historical call-edge counts (ambiguous 40, none 3) are from 27.5.1 and are not current type debt.

5. **Answers live on every node.** The checker should record its answer on every node it types, per specialization context.
   - Gate: coverage counter (report-only).
   - Status after Phase 27.5.5: consumer type queries are **100% PRESENT** in the final tracked-source emission sweep for both emitter and optimizer; Fast Debug type fact presence is enforced by its registered perturb/drop matrix. Exact per-consumer totals and node-kind breakdown are in the Phase 27.5.5 report. This replaces the historical 9% measurement from Phase 27.5.2.

6. **Code created after checking carries answers.** Desugaring, optimizer rewrites and Fast Debug invocations are built as nodes and are either checked or carry checker-consistent answers.
   - Gate: covered by the consumer gates of invariants 1, 3 and 4.
   - Status after C2: Fast Debug, emitter and optimizer have zero expression-text reparses and text lookups on the fixed workload. The tree-to-text-to-tree driver rewrite debt remains under invariant 2.

7. **Engines agree.** Fast Debug, native and optimized native produce the same observable behavior: output, exit status, failures and trace events.
   - Gate: three-engine check, Fast Debug/native parity, .mossi parity (in make check).
   - Status: output and exit status **met**. Pending: trace-event parity; seeded program generator (after 27.6).

8. **New decisions ship with their gates.** Any new semantic decision class, including those introduced by language features (Graph DSL, ownership/S3) or new backends (C++/GPU), ships in the same change with its checker-side authority and its perturbation and drop tests registered in make check.
   - Gate: AGENTS.md rule plus review.
   - Status: rule recorded; enforcement by review.

## Maintenance

- Kept current like the language docs. Required reading for Moss compiler development (see AGENTS.md); not required for regular Moss development. Fed to cold-start reviewers alongside the language docs.
- Every phase that changes a status or a debt number updates this file in the same delivery.
- `make check` runs the default gates and runtime ratchet. `make check-shadow`
  runs the same suites with shadow cross-checks and is required at every phase
  checkpoint and before every merge to `main` (recorded in AGENTS.md).
- The runtime ratchet gates invariants 1–3. It records re-parses, text-node and
  text-cache lookups, and text-to-tree rebuilds per stage on a fixed committed
  workload; Fast Debug, emitter and optimizer counters must remain zero. A
  counter increase fails; a decrease requires lowering the committed baseline
  in the same change.
- Retire shadow cross-check mode only after invariant 1 is met by the checker
  and invariant 2 is met by driver rewrites. Until then, shadow remains
  available separately and its measured cross-checks are limited to checker
  work and driver rewrites.
- CI should verify that every gate named here exists in make check or
  make check-shadow.
