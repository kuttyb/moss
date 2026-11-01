# Phase 27.5.4 delivery report

## C1 — Fast Debug retained-tree execution

C1 implementation: `0e150dcef0a0be69c1502710e38c1dabb4f0877e` on
`phase27-5-4-work`. Agent 2 reviewed that exact implementation and returned
**GO** before delivery. C2 emitter/optimizer migration is in progress; its final gates have not run.

Fast Debug expression evaluation now requires a retained checked tree handle;
`src/interpreter.hpp:639–670` reports `MOSS_INTERNAL_INVARIANT` when the
handle is absent. Identifier handling reads the active `est::Identifier`
node directly (`src/interpreter.hpp:2516–2538`); it no longer calls a
text-based expression-tree lookup. The retained-tree/parity and string-debt
gates are registered in `Makefile:151–153`.

The test-build fact hooks cover call targets, specializations, joins, and
patterns. `MOSS_TEST_PERTURB` invalidates or perturbs the checked answer and
`MOSS_TEST_DROP_FACT` removes it; all three engines must fail closed for the
accepted fixtures. The harness has three fixtures per class and runs in both
EST modes (`tests/tooling/check_phase27_5_4_fact_hooks.py`). Agent 2 selected
and verified additional fixtures:

- Call targets: `tests/phase27/est/pipeline_shape.moss`
- Specializations: `tests/tooling/fixtures/generic_specialization/twice_multi.moss`
- Joins: `tests/tooling/fixtures/phase27_5_1/join_recover_binding.moss`
- Patterns: `tests/tooling/fixtures/phase27_5_1/pattern_parenthesized.moss`

The non-test `moss-opt` ignored the hook variables. The three-engine parity
harness passed for `examples/functional_basics.moss`,
`tests/phase27/est/specialized_call_facts.moss`, and
`tests/swarm_001_parenthesized_pipeline.moss`.

`make --jobs="$(nproc)" check` passed in default and shadow modes, with 165/165
jobs in each. The registered hook harness passed in both modes. The first
post-hook full-suite attempt had 5 failures from callable Identifier handling;
parent comparison confirmed the regression, it was fixed by consuming the
checked node directly, and the complete rerun passed. The Fast Debug consumer
reparse and text-node lookup counts are 0: Agent 2 confirmed no Fast Debug
counter call sites remain in `interpreter.hpp` or `interpreter_domains.inc`,
and the test-build coverage sample reported zero Fast Debug reparses and text
lookups. The retained-tree path has no fallback parser or text-lookup branch.

Known debt for expression types, pipeline types, and ownership remains listed
in `docs/COMPILER_STATE.md`. The class-1 coverage percentage is report-only;
this C1 checkpoint does not claim emitter/optimizer migration. No separate
performance suite was run.


## C2 progress — retained callback type queries

At the 2026-11-01 checkpoint, functional callback result-type queries consume
retained stage argument ExprIds. On a targeted four-source/default-plus-`-O`
sample, the c96 parent measured 17,129 text-node lookups and 9,047 native
expression-view reparses; the candidate measured 8,235 and 5,052. Both had zero
native pipeline reparses. Generated Rust was byte-identical for all eight
source/profile comparisons. These sample counts are not the required final
corpus + examples + packages measurement. No C2 full suite/performance gate has
run.
