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

Assignment string-concatenation collection now walks the checked RHS Binary
nodes and retains ExprIds for operands during type, equality/mention, literal
and emission queries. `tests/phase24_string_concat.moss` produces byte-identical
Rust to C1 in default and `-O`; rustc and native output (`string concat ok`)
match in both modes. Its current optimized candidate sample is 2,161 text
lookups and 715 expression-view reparses; a paired parent count remains
pending.

The retained-node emitter now lowers String `append`, `reserve`, and checked
String builtin-method targets including `length` without falling back to the
expression-text emitter for this fixture. Its optimized counts fell
2,161/715 -> 1,678/356 against the immediately preceding candidate. Default
and `-O` generated Rust remain byte-identical to C1; both Rust programs compile
and print `string concat ok`. The test trace has no emitter-node fallback for
this fixture. These are targeted counts, not the final paired corpus result.

Four accepted string/write-through fixtures (`phase24_string_concat`,
`swarm_049_string_builtin_operators`, `swarm_030_string_comparison_borrowed`,
and `repro6_string_concat_native`) also remain byte-identical to C1 in default
and `-O`; all eight generated Rust files compile and native stdout matches.
`swarm_044_string_literal_binary_lexing` is rejected by C1 with
`UNSUPPORTED_STRING_ORDERING`, so it was excluded from this parity set.

## C2 progress — emitter consumes checked call edges on nested nodes

When nested call nodes lacked a retained resolved-target fact, the emitter now
uses the checker's `SemanticCallEdge` for the active body context and source
line. The emitter does not infer the target from runtime or consumer-computed
argument types. Static specialization result types are taken from the checked
specialization candidates when uniquely available. This also restores the
parent's wrapping integer lowering in `specialized_call_facts.moss`.

For the same fixture and default emission command, the c96 parent rebuilt with
matching parser-site instrumentation measured 3,387 text-node lookups and
2,685 expression reparses; the candidate measured 777 and 728, respectively
(−77% and −73%). Generated Rust is byte-identical, rustc compilation and
runtime succeed (`2`, `2`), and three-engine output matches. The four-class
perturb/drop matrix passes. This is a one-fixture sample; bulk counts and full
C2 gates remain pending.

## C2 progress — specialized method calls use checked member requirements

For generic caller specializations whose retained call node lacks a direct
resolved-target fact, the emitter now reads the matching
`FunctionSpecialization.member_requirements` entry and its checker-selected
`selected_callable_id`. It maps that identity to the declared method and lowers
the node directly; an absent or ambiguous checked target does not choose a
method from runtime values. This removes nested specialized method-call
fallbacks in `specialized_call_facts.moss`.

With the same default command and test-build parser-site counters, the c96
parent measured 3,387 text-node lookups / 2,685 reparses; the candidate
measured 519 / 470 (−85% / −82%). Generated Rust is byte-identical; rustc and
runtime succeed (`2`, `2`), and three-engine parity plus the fact-hook matrix
pass. This remains a targeted sample, not the required bulk measurement.

## C2 progress — mini application paired consumer counts

On `examples/mini_application.moss`, with the same test-build parser-site
instrumentation and default / `-O` generation commands, c96 reports 9,497
text-node lookups and 6,122 expression reparses in each mode; the candidate
reports 4,673 and 3,172 (−51% / −48%). Generated Rust is byte-identical in
both modes. The fixture retains the previously documented Fast Debug
unsupported user-named pipeline-stage baseline limitation; this sample is
emitter count and Rust-output evidence only. No bulk suite or performance run.

## C2 progress — checked callable pipelines lower from nodes

The emitter now handles user-named pure function-call pipelines directly from
`Pipeline` and `Call` nodes. It reads each stage's checked target and the
checker-selected specialization; the fast path is restricted to pure,
non-raising, non-WRITE calls with literal/identifier explicit arguments and
falls back for other shapes. Call-target and specialization drop/perturb hooks
fail closed on this path.

On `examples/mini_application.moss`, matched c96/candidate default and `-O`
generation produces byte-identical Rust and identical compiled output. Counts
per mode changed from parent 9,497 text-node lookups / 6,122 reparses to
candidate 3,507 / 2,018 (−63% / −67%); native functional-pipeline reparses
fell 2 -> 0. The four-class hook matrix passes, and direct drop/perturb probes
for call targets and specializations on this fixture all fail with
`MOSS_INTERNAL_INVARIANT`. Other emitter reparses remain; C2 full gates are
pending.

## C2 progress — local and state identifiers lower directly from nodes

`EmitterExpr` identifier lowering now emits checked local bindings directly
once view-aware projection has had its chance, and maps non-view domain state
and route names to their existing Rust representation without re-parsing the
identifier text. On `examples/mini_application.moss`, both default and `-O`
generated Rust remain byte-identical to c96. Paired test-build counts per
command are parent 9,497 text-node lookups / 6,122 expression reparses and
candidate 3,031 / 1,610 (−68% / −74%). The emitter node-fallback counter has
no remaining expression shapes on this fixture. Three-engine and the
four-class perturb/drop hook matrix pass.

A direct member-call lowering experiment was reverted: on
`examples/collections_and_methods.moss`, lowering `jobs[1].weighted(3)` from
its node changed generated Rust by introducing a clone on the indexed
receiver. That is an ownership decision and outside this phase's allowed
scope. This fixture therefore remains evidence of a member-call fallback,
not a completed migration. Full suites, corpus consumer counts, and
performance gates remain pending.

## C2 progress — checked member calls retain receiver place lowering

The emitter now uses a unique checked call-edge target when a retained member
call node lacks a direct target fact. For indexed Vector/Seq method receivers,
it renders the index as a method place rather than using value-context index
lowering, preserving the prior Rust autoref spelling and avoiding the extra
clone observed in the first experiment. `examples/collections_and_methods.moss`
now has no emitter node-fallback shapes. Its generated Rust is byte-identical
to c96 in default and `-O`, compiles with rustc, and prints the same output.

Paired counts per command are parent 2,607 lookups / 1,259 expression
reparses and candidate 855 / 194 (−67% / −85%) in both modes. Three-engine
and the four-class perturb/drop matrix pass. Map-indexed or other
ownership-sensitive method receivers are not covered by this narrow path.

## C2 progress — nested pipeline nodes and checked exceptional identity

The emitter now builds pure functional pipeline descriptors directly from
retained `Pipeline`/`Call` nodes, preserving stage ExprIds. A temporary parsed
pipeline owns its arena so callback nodes cannot dangle after returning from
the compatibility parser. Eager map lowering consults the retained stage type
before callable-result fallback. This fixes the grouped `map`/`filter` lowering
fixture and the checked pipeline paths in GraphKit without changing generated
program output. `assoc-grouped.moss` default and `-O` Rust compiles and prints
`3`.

Two targeted C2 regressions were repaired. A raise value whose direct EST type
fact is unavailable now uses the type identity in the checker's exceptional
alternatives (when those alternatives name one enum type); `phase21_peer_effects`
compiles and matches its expected output. Specialized member selection filters
checked member requirements against the checked receiver type, resolving the
trait-parameter sample (`10`). The initial failures were regressions relative to
the C1 binary. `swarm_049b_fast_debug_map_call_argument` also emits, compiles,
and prints `4`; its Fast Debug missing checked-expression-node error reproduces
on C1 and is baseline debt. The incomplete-index adversarial probe continues to
fail closed on a missing checked handle, while C1 emits it; it is an incomplete
probe rather than a passing source fixture.

The prior paired corpus counts are from commit `ca03f1ab`, before this node
pipeline and regression-fix slice: 5,929,515 lookups and 3,534,743 reparses
candidate versus 15,167,953 and 9,750,215 at c96, with candidate per-file
emission failures 1,427 vs 1,406. These aggregate counts are stale for the
current tree and must be rerun once the C2 consumer migration is complete.
No full suite or performance run has been done for this slice.

## C2 progress — paired corpus measurement and checked callback result

At `63156e99`, the paired corpus emitter pass covered 1,822 tracked Moss
sources and 337 package manifests (3,644 standalone default/`-O` commands per
binary). Against the counter-matched c96 build, text lookups fell
15,167,953 -> 5,980,690 (−60.6%), expression reparses fell 9,750,215 ->
3,514,024 (−64.0%), and native functional-pipeline reparses fell 7,362 -> 798.
Candidate emission failures were 1,410 vs 1,406. Two were the
`swarm_049a_fast_debug_map_in_method` callback output type; the emitter now
reads the selected method identity from the checked call fact/edge and obtains
the declared result type. Default and `-O` generated Rust are byte-identical to
C1, compile, and print `2`. On this one fixture, default lookups/reparses fell
1,521/1,173 -> 434/338.

The other two extra failures were the incomplete-index probe in both modes.
The checker accepts this truncated source, but C1 emits Rust that rustc rejects
with an unclosed `items[` delimiter. The candidate now fails closed on its
missing checked `RhsValue` handle instead of emitting the invalid artifact.
Remaining full-suite parity must confirm this malformed-source diagnostic
change is outside behavioral coverage. The corpus figures above precede the
map-method fix and are not the final paired count.

## C2 progress — semantic pipeline consumers use planned ExprIds

The shared traversal emitter now reads the checked RHS handle and traverses its
Pipeline/Call nodes. Virtual upstream stages resolve their retained expression
plan by pipeline ID and lower from that node, rather than reparsing the plan's
source string. Targeted functional-tree tests pass; mini_application and
GraphKit emit successfully. Mini-application default Rust remains byte-identical
to C1; its paired counts remain 9,497 -> 3,031 lookups and 6,090 -> 1,610 total
reparses (native functional reparses 2 -> 0). The full corpus recount remains
pending until the remaining consumer paths are migrated.

## C2 progress — source-scoped call facts and GraphKit node lowering

Some checker facts are recorded under `source:<file>:<line>` while native
codegen executes under a method identity. The emitter now consults that exact
source context, and uses the checked builtin Map.get target and checked
`SemanticCallEdge` target for sibling method calls. It lowers typed empty
Vector/Seq constructors from their retained type-argument nodes. GraphKit
default and `-O` generated Rust compiles and runtime output matches C1; Rust
text differs only in redundant parentheses around a map place. The GraphKit
test-build trace went from 44 call + 30 constructor node fallbacks to zero.
Its current standalone default counters are 38,180 text lookups and 9,580
native expression-view reparses (functional pipeline reparses zero). Three-
engine check and the call-target/specialization/join/pattern fact hook matrix
pass. These are targeted results; full suite and final paired corpus count
remain pending.

### Optimized virtual-upstream regression checkpoint (2026-11-01)

The emitter now locates a virtual upstream pipeline through its owning checked
statement plan and retained ExprId, including plans owned by a different
statement than the current lowering site. The lookup uses the plan's recorded
slot to carry the corresponding source view for diagnostics and rendering; it
does not choose by source line or parse the plan's expression text. All 30
optimized functional fixtures in the prior failure delta now emit successfully
(the 12 remaining failures after the first handle correction and 18 already
passing fixtures retested). This targeted result has not yet been checked by a
full suite or paired corpus recount.

Sibling-call arguments now also travel as `EmitterExpr` values into method ABI
lowering. On the same GraphKit default command, text lookups/reparses fell
38,180/9,580 -> 37,596/8,996 with unchanged runtime output and no node fallback
events. This remains a targeted improvement only; the corpus still has
unmigrated emitter type/string consumers.

Assignment RHS shape queries now have direct retained-node overloads for
identifier, literal, empty-constructor and index nodes. A targeted
`functional_semantic_optimization.moss -O` command decreased text lookups
991 -> 986 while reparses remained 758, showing that other string-fragment
consumers dominate this fixture's reparse count. Six assignment/collection
fixtures compiled in default and `-O`; the optimized functional 30-fixture
set, fact-hook matrix and three-engine fixture passed. Full C2 gates remain
pending.
