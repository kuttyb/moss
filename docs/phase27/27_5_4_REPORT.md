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

## C2 progress — checked functional stage output types

The emitter now carries planned functional stage output types into mapped-stage
lowering and no longer infers the mapped element type from callback source in
shared, eager, or fused traversals. Missing planned map output types fail with
an internal invariant. On the same targeted four-source/default-plus-`-O`
sample, counts changed from the preceding candidate's 8,235 text-node lookups
and 5,052 expression-view reparses to 7,914 and 4,752; the pinned parent was
17,129 and 9,047. Both had zero pipeline reparses. Three-engine checks, the
functional-tree harness, and eight generated-Rust comparisons passed. Final C2
corpus counts and suites remain pending.

A subsequent small slice routes native `Stmt::Kind::Raw` expression lowering
through the retained `ExprSlot::Expression` root. Its focused checks pass. The
four-source counter sample did not change (7,914 / 4,752 / 0), since the sample
does not exercise that statement kind; broader counters remain due at C2.

Functional lowering's entry point now accepts `ParsedFunctionalPipeline`
directly, eliminating its expression-string parameter and optional internal
parse path. Focused tests and targeted Rust parity pass. The four-source counts
remain 7,914 text-node lookups / 4,752 expression-view reparses / 0 pipeline
reparses; the existing checked projection meant this API cleanup did not affect
that sample. The legacy string `expr` entry remains a reparse path.

A retained-node string-concatenation lowering now flattens nested checked
string `+` nodes and emits operands directly. The Phase 24 string-concat
fixture passes three-engine parity and matches parent generated Rust in default
and `-O`. Its paired two-profile counters fell from 11,550 lookups / 4,276
expression reparses to 5,724 / 1,936. Final corpus measurements remain pending.

Checked object method calls in expression nodes now consume their resolved
`method:Owner.name` target and matching method declaration for lowering. The
four-source/default-plus-`-O` sample fell to 7,782 text-node lookups and 4,688
expression-view reparses (parent 17,129 / 9,047; prior candidate 7,914 / 4,752);
pipeline reparses remain zero. Three representative method examples generate
parent-identical Rust in both profiles. The four-class perturb/drop harness
passes; full C2 gates remain pending.

The C2 method-call target path is now covered by the perturb/drop harness using
`examples/collections_and_methods.moss`. The emitter fails closed when its
checked method target is dropped or replaced by the synthetic perturbation.
Call-target coverage has four fixtures; the other required classes retain three
each. The registered test passes for Fast Debug, native, and `-O`; the normal
method examples pass three-engine execution. The expected perturbed/dropped
behavior is a `MOSS_INTERNAL_INVARIANT`, so a successful emission or run fails
the test.

Recursive binary expression lowering now keeps operand identity as ExprIds for
borrowed-message-parameter and integer-to-float checks. Six fixtures produce
byte-identical Rust versus c96 in default and `-O`; targeted three-engine and
functional-tree checks pass. The original four-source/default-plus-`-O` sample
improved from 7,782/4,688 to 6,940 text-node lookups/expression reparses; the
expanded six-source sample is parent 33,442/16,611 versus candidate
14,646/7,340. Pipeline reparses remain zero. The required full workload and
suite/performance gates remain pending.

## C2 progress — expression-node lowering and perturb/drop coverage (2026-11-01)

C2 is still in progress. Borrowed-view checks now traverse retained member
nodes; known-function view-wrapper selection and parameter-effect handling use
argument ExprIds; checked `Message` expressions have a node-based lowering
path. Root-ingress messages and other legacy string-lowering shapes remain.
The candidate generated Rust was byte-identical to the c96 parent on seven
focused fixtures in default and `-O`. On that same paired sample, counters
improved from 44,723 text lookups / 24,163 expression-view reparses on c96 to
22,951 / 13,885 on the candidate after the checked-argument slice. These are
focused counts, not the required corpus + examples + packages workload.

The make-check perturb/drop harness now runs the Agent 2-selected fixture for
each class as well as the original fixture set: call targets 5, specializations
4, joins 4, patterns 3. Default and shadow hook runs pass. Expected behavior
for perturbation and drop is fail-closed `MOSS_INTERNAL_INVARIANT`; successful
Fast Debug/native/`-O` execution or emission is a test failure. Expression
types, pipeline types, and ownership remain documented known consumer debt.
Neither full C2 suite nor performance validation has run.

A further four-source paired sample (collections, empty-map context, typed
string-map indexing, and mini application; default plus `-O`) measured 23,204
text lookups / 14,138 expression-view reparses on c96 and 12,394 / 7,446 on the
candidate. The generated Rust is byte-identical on all four. Node lowering for
empty Map/Set/Queue and Some constructors is active; checked-call wrapper
selection uses node argument facts. A mini-application parity comparison
caught an operator-precedence difference for a borrowed object receiver; the
emitted receiver is now parenthesized and the focused default/`-O` parity
comparison passes. These remain targeted samples, not the final workload.

Common global builtins (`sqrt`, `is_nan`, `is_finite`, numeric conversions,
`sum`, `Char`, and scalar less/equality/hash) now lower directly from retained
Call argument nodes. On a temporary accepted `to_int`/`sum` probe, the parent
measured 702 text lookups / 557 expression reparses in each profile; the
candidate measured 132 / 125. Default and `-O` generated Rust is byte-identical
to c96, compiles, and runs with output `3`. The temporary source is under
`tmp/`, not registered as a suite fixture.

Checked scalar and string builtin member calls now lower from EST receiver and
argument nodes, gated by the recorded builtin call target. The focused four-
source/default-plus-`-O` sample measured 20,514 / 11,124 on c96 versus
11,354 / 6,272 on the candidate (text lookups / expression reparses). Rust
output is byte-identical and Fast Debug, native, and `-O` compile-and-run
outputs agree on the four sources. Mutating string methods and other receiver
families remain on the legacy path.

`generated_expr_type(EmitterExpr)` no longer falls back to the legacy string
inference routine: checker facts and recursive EST-node rules supply the answer,
and an unsupported shape returns no type. On ten targeted sources, default plus
`-O`, c96 measured 76,824 text lookups / 43,213 expression reparses and the
candidate measured 41,205 / 25,257; all twenty generated-Rust outputs are
byte-identical. Fast Debug/native/`-O` agree on nine sources. The tenth,
`examples/mini_application.moss`, has a pre-existing unsupported user-named
pipeline stage on c96 (unsupported-stage error at line 88); current retained-tree
Fast Debug reports a missing checked node for that stage. This remains recorded
baseline Fast Debug debt and is outside the emitter changes. The full C2 suite
has not run.

The test-only type-fact coverage instrumentation no longer resolves a node by
text fingerprint or reparses text just to classify a `generated_expr_type`
query. Such text-only queries are counted as `text-query/ABSENT`. The opt-in
coverage test remains output-neutral. On the ten-source paired workload, this
reduces candidate text lookups from 41,205 to 40,133; expression-view reparses
remain 25,257, against c96's 76,824 / 43,213.

Root-ingress `Message` expressions now lower their receiver and payloads from
retained ExprIds through executor invocation. On five focused message fixtures,
c96 measured 6,408 / 2,878 and the candidate 4,503 / 2,015 (text lookups /
expression reparses); Rust is byte-identical in default and `-O`. Four fixtures
pass Fast Debug/native/`-O` runtime parity. The FileIO cleanup fixture retains
the documented Fast Debug unsupported-FileIO result while generated Rust
matches c96.

## C2 progress — count previously uninstrumented emitter parser paths

The borrowed-view text helper now routes through the counted central expression
view selection, and named-argument parsing increments the native expression
reparse counter in test builds. This changes accounting visibility only. The
candidate builds; the EST instrumentation output-neutrality gate and focused
three-engine test for `examples/collections_and_methods.moss` pass, with Rust
output byte-identical to c96. Candidate default counts are 1,244 text-node
lookups and 532 native expression-view reparses. Since c96's existing
instrumentation omitted these same direct parser sites, this is not a strict
paired delta. C2 full suites, corpus measurement and performance gates remain
pending.
