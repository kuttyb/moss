# Moss compiler contract (rev 5, Phase 27 closed at phase27-5-5-m1)

The compiler counterpart to the Moss language docs: the architectural invariants every stage must hold. Each invariant names the gate that enforces it and its current status. Where an invariant is not yet met, the gap is recorded as **declared known debt** with its current measurement. Debt may shrink; it must not grow.

Terms:
- **Checker:** the front end that decides Moss semantics.
- **Consumers:** every stage after the checker: Fast Debug, the native emitter, the functional optimizer.
- **Executing and lowering are not deciding.** Computing a + b, or choosing Rust syntax for a checked decision, is allowed. Choosing which + or which function is a decision.

Phase 27 closed at `phase27-5-5-m1`. Its final suite, coverage, counter and
performance measurements are recorded in
[the Phase 27.5.5 report](phase27/27_5_5_REPORT.md) and
[COMPILER_STATE.md](COMPILER_STATE.md). The debt entries below own the
remaining work; do not infer new targets from historical intermediate reports.

## Declared debt

Each row gives the current measured value or pending status, its registered
gate, and its owner. Detailed contexts and fixture identities are in the
linked gate data or Phase 27.5.5 report.

<!-- declared-debt:start -->
| Entry | Current value | Gate | Owner |
| --- | --- | --- | --- |
| <a id="d-checker-text"></a>`D-CHECKER-TEXT`: checker expression string passes | 55,160 reparses and 16,465,599 text-cache lookups on the fixed workload | `check_phase27_5_4_runtime_ratchet.py` via `make check` and `make check-shadow` (report-only checker counters) | Unscheduled |
| <a id="d-driver-tree"></a>`D-DRIVER-TREE`: driver text rewrites and tree rebuilds | 748 rebuilds on the fixed workload | `check_phase27_5_4_runtime_ratchet.py` via `make check` and `make check-shadow` | Unscheduled; the residual of the Phase 27 tree migration |
| <a id="d-ownership-facts"></a>`D-OWNERSHIP-FACTS`: ownership and parameter-effect consumers | 10 ownership entries in the frozen perturb/drop debt baseline | `check_phase27_5_4_fact_hooks.py` and `phase27_5_4_fact_hook_debt.json` via `make check` and `make check-shadow` | Planned, Phase 28 S3 ownership |
| <a id="d-lock-test"></a>`D-LOCK-TEST`: lock-requirement perturb/drop tests | Pending; no lock-class case in the fact-hook matrix | `check_phase27_5_4_fact_hooks.py` via `make check` and `make check-shadow`; lock-class coverage remains pending | Unscheduled |
| <a id="d-trace-parity"></a>`D-TRACE-PARITY`: trace-event parity | Pending | `make check` and `make check-shadow`; the current three-engine check does not compare trace events | Unscheduled |
| <a id="d-program-generator"></a>`D-PROGRAM-GENERATOR`: seeded program generator | Pending after Phase 28 | `make check` and `make check-shadow` when registered | Planned, after Phase 28 |
| <a id="d-baseline-suite"></a>`D-BASELINE-SUITE`: known full-suite failures | 15 failures on `origin/main` a71fe9b9 (Default and Shadow 150/165); candidate `moss-copy-final` 151/166 in both, with the same failures: `check_phase20_executor_fileio`, `check_phase21_lock_fileio_adversarial`, `check_phase21_standard_error_namespace`, `check_phase25_conformance`, `check_phase26_0_1_c3_parse_cache`, `check_phase27_accepts_rustc`, `check_teaching_diagnostics`, `phase20_review_probes`, `shard_concurrency`, `shard_enum_modules`, `shard_fast_debug`, `shard_phase25_int_payloads`, `shard_phase5_tooling`, `shard_showcase`, `shard_swarm_cases` | `make check` and `make check-shadow` | Unscheduled |
| <a id="d-accepted-rust"></a>`D-ACCEPTED-RUST`: checker-accepted sources that do not compile as Rust | 6 active allowlist records including Group A and `mutate_loop_element`; additionally `swarm_035` (E0499), Group A, T6, `mutate_loop_element`, and the two C1 probes emitted invalid Rust | `check_phase27_accepts_rustc.py` via `make check` and `make check-shadow`, with the ratcheting allowlist | Unscheduled |
<!-- declared-debt:end -->

## Invariants

1. **One parse (consumers).** Consumers never parse expression text. Each expression is parsed once, at the initial parse of its source file or the initial load of an imported .mossi body; consumers navigate the retained tree. Diagnostics, fmt and edit tools are exempt.
   - Gate: `check_phase27_string_debt.py` and `check_phase27_5_4_runtime_ratchet.py` in `make check` and `make check-shadow`.
   - Status after Phase 27.5.5: Fast Debug, emitter and optimizer **met** for consumer parsing. The final paired emission sweep covers 1,828 tracked sources in default and `-O`; consumer type-inference requests and consumer reparses measured zero. Checker parsing is intentionally still string-based and is outside the consumer target.
   - Measured checker count on the fixed workload: 55,160 expression reparses and 16,465,599 text-cache lookups. Per the Phase 27.5.4 consumer-only re-scope, these checker-internal counts are report-only; the ratchet gates driver rewrites and consumers. Consumer limits are committed in `tests/tooling/fixtures/phase27_5_4_runtime_baseline.json`; the workload includes calculator, regex and string-concatenation programs.

2. **The tree is the source of truth.** Rewrites after parsing should operate on tree nodes; expression text should be derived from the tree, never the reverse.
   - Gate: `check_phase27_5_4_runtime_ratchet.py` in `make check` and `make check-shadow`.
   - Status: **not met.** Driver rewrites still edit text and rebuild expression trees. The fixed workload records 748 driver rebuilds; the committed baseline prevents increases. The C2 retained-tree consumer migration does not change the driver's rewrite representation.

3. **Nodes are passed, not text (consumers).** Consumers pass node IDs, never look up a node by expression text or text fingerprint, and never consult a text-keyed cache.
   - Gate: `check_phase27_5_4_runtime_ratchet.py` in `make check` and `make check-shadow`; Fast Debug, emitter and optimizer must remain zero. Checker-internal text-cache lookups are reported but not gated under the 27.5.4 re-scope.
   - Status after Phase 27.5.5: Fast Debug, emitter and optimizer **met** (zero text-node lookups on the fixed workload; the final 1,828-source emitter sweep also checks both default and `-O`). The checker still consults text-keyed caches; its current fixed-workload 16,456,102 lookups remain measured under invariant 1 and are report-only under the consumer-only re-scope.

4. **The checker is the only semantic authority.** Only the checker decides: expression types, call targets, specializations, pipeline types, parameter effects and passing, clone/move/borrow, control-flow joins, pattern meaning, lock requirements. Consumers execute or lower those decisions; they never make them.
   - Gate: `check_phase27_5_4_fact_hooks.py` in `make check` and `make check-shadow`, with per-class perturbation and drop cases.
   - Status: expression types and pipeline-stage/output types **met** in Fast Debug, native emission and `-O`; call targets, specializations, joins and patterns remain covered by the C1 perturb/drop tests. The Phase 27.5.5 report records additional unseen reviewer-selected type fixtures and their outcomes. Lock requirements: test pending.
   - Known debt (frozen; the gate fails if the set of non-following engines grows): parameter effects/passing and clone/move/borrow, owned by Phase 28 S3. There are no expression-type or pipeline-type entries in the debt JSON. Historical call-edge counts (ambiguous 40, none 3) are from 27.5.1 and are not current type debt. See [Declared debt](#declared-debt).

5. **Answers live on every node.** The checker should record its answer on every node it types, per specialization context.
   - Gate: `check_phase27_est_coverage.py` in `make check` and `make check-shadow` (coverage remains report-only).
   - Status after Phase 27.5.5: consumer type queries are **100% PRESENT** in the final tracked-source emission sweep for both emitter and optimizer; Fast Debug type fact presence is enforced by its registered perturb/drop matrix. Exact per-consumer totals and node-kind breakdown are in the Phase 27.5.5 report. This replaces the historical 9% measurement from Phase 27.5.2.

6. **Code created after checking carries answers.** Desugaring, optimizer rewrites and Fast Debug invocations are built as nodes and are either checked or carry checker-consistent answers.
   - Gate: `check_phase27_string_debt.py`, `check_phase27_5_4_runtime_ratchet.py`, and `check_phase27_5_4_fact_hooks.py` in both check modes.
   - Status after C2: Fast Debug, emitter and optimizer have zero expression-text reparses and text lookups on the fixed workload. The tree-to-text-to-tree driver rewrite debt remains under invariant 2.

7. **Engines agree.** Fast Debug, native and optimized native produce the same observable behavior: output, exit status, failures and trace events.
   - Gate: `check_phase27_three_engine.py`, the Fast Debug/native parity and `.mossi` jobs in `tests/run_parallel.py`, run by `make check` and `make check-shadow`.
   - Status: output and exit status **met**. Pending: trace-event parity and the seeded program generator after Phase 28; see [Declared debt](#declared-debt).

8. **New decisions ship with their gates.** Any new semantic decision class, including those introduced by language features (Graph DSL, ownership/S3) or new backends (C++/GPU), ships in the same change with its checker-side authority and its perturbation and drop tests registered in make check.
   - Gate: `check_phase27_5_4_fact_hooks.py` in `make check` and `make check-shadow`; review confirms new classes are included.
   - Status: rule recorded; enforcement by review.

9. **Accepted programs compile.** Every program the checker accepts must produce Rust that compiles.
   - Gate: `check_phase27_accepts_rustc.py` in `make check` and `make check-shadow`, using its ratcheting allowlist.
   - Status: **not met.** Known baseline cases remain in [Declared debt](#d-accepted-rust), including Group A, `mutate_loop_element`, `swarm_035` (E0499), and the C1 probes that emitted invalid Rust. T6 has no active source fixture.

## Maintenance

- Kept current like the language docs. Required reading for Moss compiler development (see AGENTS.md); not required for regular Moss development. Fed to cold-start reviewers alongside the language docs.
- The companion [compiler architecture guide](COMPILER_ARCHITECTURE.md) describes current stage boundaries and data structures; this contract remains the authority for numbered invariants and their statuses.
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
- `check_contract_gates.py` runs in `check-preflight` and verifies every named
  gate script is registered in `make check` or `make check-shadow`.
