# Compiler state after Phase 27.5.2

This records the compiler's checked facts, remaining independent decisions, text-reader inventory, and verification boundaries at Phase 27.5.2. It describes existing behavior and evidence; it does not specify a refactor.

## EST mode and the original 39 mode gates

Default mode is `On`: expression handles and semantic facts are retained without legacy-parser comparisons. `MOSS_EST=shadow` adds those comparisons. `off` is rejected. `Makefile` runs `tests/run_parallel.py` with `MOSS_EST` unset and with `MOSS_EST=shadow`.

The 39 gates at base `phase27-5-1-m1` (`e7a9f707`) classify as follows. Line numbers identify their locations at that base revision.

| Component | RETENTION | CROSS-CHECK |
| --- | --- | --- |
| `src/moss.cpp` | 13708, 13725: rebuild expression handles after rewrites | — |
| `src/semantic_checker.cpp` | 26144: require matching retained fact; 26339: locate root-expression cursor | 10933, 11181, 11414, 11466, 21450, 25086, 26119, 26133, 26166, 26424, 26681, 29046: old parser/type agreement, fact agreement, or diagnostic assertions |
| `src/est/expression_query.cpp` | — | 485, 555, 599, 723, 766, 777, 796, 878, 909, 936, 956, 980, 1024, 1224, 1266, 1282, 1290, 1327, 1339, 1361, 1481: EST projection compared with legacy parser/string behavior |
| `src/est/expression_tree.cpp` | 272: mode selection; now defaults to `On` | 1283: `compare_string_shape` |
| **Totals** | **5** | **34** |

In current sources, statement handle refresh is unconditional when an expression arena exists (`src/moss.cpp:13704–13726`). Checker fact writes and root cursors are active in default mode (`src/semantic_checker.cpp:26102–26380`); shadow-only comparisons remain around those writes. Functional plans retain statement and result expression handles at `src/semantic_checker.cpp:21784–21836`.

Specialized bodies record facts under the specialization context (`src/semantic_checker.cpp:26090–26100`). The regression at `tests/phase27/est/specialized_call_facts.moss` verifies ordinary call-target facts in two distinct specialization contexts. The test is `tests/tooling/check_phase27_est_specialized_facts.py`.

## Remaining local semantic decisions

The native emitter still infers expression types in `Generator::generated_expr_type` (`src/native_generator.cpp:2024`). Its results feed object-method re-resolution (`src/native_generator.cpp:1900–1909, 2700–2713`), functional callable/type resolution (`src/native_generator.cpp:1978–2015, 3367–3388`), and collection/map type reconstruction and lowering (`src/native_generator.cpp:1666–1694, 1981–2015`).

Fast Debug still has a pipeline output-type runtime fallback when the checked stage type is absent (`src/interpreter.hpp:1300–1331`). Phase 27.5 counted 12 such firings; the behavior and samples are recorded in `docs/phase27/27_5_REPORT.md`.

The shared checked call-edge lookup is `checked_call_edge_specialization` (`src/ast.hpp:945–975`, `src/native_generator.cpp:1534–1590`, `src/interpreter.hpp:1092–1107`). Phase 27.5.1 recorded 40 ambiguous edges and 3 natural `none` cases; the latter keep the pre-existing fallback. Its measured counts and classifications are in `docs/phase27/27_5_1_REPORT.md`.

Fast Debug specialization lookup still returns no unique specialization for two checked call edges; the two known gaps are recorded as G4 in `docs/phase27/27_5_REPORT.md` and Phase 27.5.1's call-edge report. The interpreter's caller detects whether the checked call edge requires specialization at `src/interpreter.hpp:2543–2553`.

The emitter makes local ownership and representation choices while lowering checked effects and types: parameter borrow/value ABI (`src/native_generator.cpp:1417–1438`), copy versus clone for sort keys (`src/native_generator.cpp:1690–1708`), argument borrow/clone and recovery-slot take/restore (`src/native_generator.cpp:1774–1823`), and recovery-slot handling (`src/native_generator.cpp:576–585`). Object-view path lowering is in `src/borrowed_views.inc:18–64`; it projects member paths from EST before selecting a Rust view.

## String fields and text operations

`Stmt` stores expression spellings and other text in the same `a`, `b`, `text`, and `args` fields (`src/ast.hpp:249–263`). The current `tests/tooling/check_phase27_string_debt.py` inventory reports these component counts:

| Text use | Component counts | What the count covers |
| --- | --- | --- |
| Expression-text parser/classifier calls | parser 23; compiler front end 48; checker 0; emitter 0; reach walk 0; Fast Debug 0; optimizer 0 | Calls to the audited legacy parser/classifier APIs. The parser and front-end totals are 23 and 48 respectively. |
| Names and flags in expression-bearing fields | checker 27; emitter 15; checker binding-name comparisons 7 | 42 audited literal comparisons against names/flags plus 7 audited field-to-binder comparisons. `consume` is among the action flags. |
| Raw shape scanners on `Stmt.a`, `Stmt.b`, or `Stmt.text` | checker 0; emitter 0 | The audit rejects direct punctuation scans, first-character dispatch, and prefix classification on these fields. |

Source text is still synthesized or edited in explicit lowering helpers such as `replace_est_placeholders` (`src/native_generator.cpp:1153–1164`) and the generated callable key expressions (`src/native_generator.cpp:1689–1697`). Place paths used for borrowed object views are projected by `tree_member_path` and `view_place` (`src/native_generator.cpp:1268–1290`, `src/borrowed_views.inc:18–64`); they are not character-scanned from `Stmt` expression fields. The string-debt counts above do not count synthesized Rust/source fragments.

## S4 consumer and fact coverage

The test-build-only counters are defined under `MOSS_TEST_BUILD` (`src/est/expression_tree.hpp:284–294`, `src/est/expression_tree.cpp:277–350`). `Makefile:18` defines that macro for `moss-dev`; the optimized compiler omits it. `MOSS_EST_COVERAGE_STATS=1` opts into reporting. The counter test `tests/tooling/check_phase27_est_coverage.py` verifies that counters are absent without the opt-in and from the optimized compiler, and that counter-enabled and counter-disabled generated Rust is identical.

The paired pass used the Phase 27.5.1 baseline behavior and default-on candidate on 1,822 `.moss` files and 337 package manifests under `tests/` and `examples/`. Counts cover standalone source emission, source Fast Debug, and package Fast Debug. Successful Margo package builds suppress the compiler's stderr counter report; package sources were also measured through standalone emission.

| Fallback site | Baseline | Default-on |
| --- | ---: | ---: |
| Native expression view (`native_generator.cpp:1124`) | 5,292,201 | 4,872,274 |
| Native functional pipeline (`native_generator.cpp:1964`) | 3,696 | 3,678 |
| Fast Debug expression tree (`interpreter.hpp:653`) | 43,011 | 35,933 |
| Fast Debug functional pipeline (`interpreter.hpp:1169`) | 218 | 217 |
| Package Fast Debug expression tree | 25,366 | 17,312 |
| Package Fast Debug functional pipeline | 213 | 191 |
| **Total** | **5,364,705** | **4,929,605** |

The total decreased by 435,100 (8.11%). Residual reparses occur when consumers inspect nested or synthesized expression fragments without an exact retained root handle; native and Fast Debug pipeline helpers parse optimizer-created or transformed fragments into temporary arenas. The paired measurement had the same counts of emit failures (11), source Fast Debug failures/timeouts (67), package build failures (73), and package Fast Debug failures (109) on both revisions.

At every `Generator::generated_expr_type` entry, the candidate counted whether the expression's exact EST root had a checked type in the active body/specialization context. Totals were PRESENT=23,452 and ABSENT=259,216. Top node kinds (PRESENT/ABSENT): call 6,242/19,169; identifier 11,277/156,498; literal 3,761/55,936; member 471/7,071; index 298/6,870. The remaining kinds are reported by the counter at `src/native_generator.cpp:2024–2095`.

## Test hooks present in release builds

Phase 27.5.1 left the `MOSS_TEST_*` fact-drop, fact-counter, and missing-expression-handle hooks in the same optimized compiler binary used as `moss-release`; the report records that hook gating was skipped because test and release entrypoints share that binary (`docs/phase27/27_5_1_REPORT.md`, W4). Current hook sites include `MOSS_TEST_DROP_FACT` in `src/native_generator.cpp:1539,5655`, `MOSS_TEST_COUNT_FACT` in `src/native_generator.cpp:1545–1575` and `src/interpreter.hpp:1304–1307,2546–2550`, and `MOSS_TEST_MISSING_EXPRESSION_HANDLE` in `src/functional_optimizer.cpp:673`. The S4 coverage counters are separate and test-build-only.

## Existing gates and what they establish

| Harness | Evidence boundary |
| --- | --- |
| `tests/tooling/check_phase27_string_debt.py` | Audits expression-text reader calls and the remaining name/binder comparisons on `Stmt` fields. |
| `tests/tooling/check_phase27_est.py` | Exercises EST queries and checks that default and shadow retain the same checked outcomes. |
| `tests/tooling/check_phase27_est_specialized_facts.py` | Verifies checked call-target facts are attached in distinct specialization contexts. |
| `tests/tooling/check_phase27_est_coverage.py` | Verifies the coverage report is opt-in/test-build-only and does not change generated Rust. |
| `tests/tooling/check_phase27_est_mossi.py` | Compares `.mossi` output under default and shadow on the same compiler. |
| `tests/tooling/check_phase27_mossi_baseline.py` | Recomputes package fingerprints and execution-body hashes under the baseline toolchain identity, then compares the remaining `.mossi` bytes. |
| `tests/tooling/check_phase27_three_engine.py` | Checks the native, Fast Debug, and optimized execution outputs for its conformance fixture. |
| `tests/tooling/check_phase27_5_3_fastdebug_checked_tree.py` | Checks three targeted accepted fixtures for native/Fast Debug/`-O` output parity. |
| `make check` | Runs string-debt, EST facts/coverage, and the registered repository tests with `MOSS_EST` unset and with `MOSS_EST=shadow` (`Makefile:150–158`). |

The Phase 27.5.2 source parity measurement compared all 1,822 source verdicts, 1,130 successful default Rust emissions, 1,130 successful `-O` Rust emissions, and 763 Fast Debug runs against the baseline. No differences were found. Fast Debug timed out identically on `examples/frontend_syntax.moss` and `examples/functional_pipeline.moss` on both revisions. The `.mossi` identity recomputation passed for its library/app fixture pair. Package-context parity is recorded in the Phase 27.5.2 delivery report.

## Phase 27.5.3 authority inventory

The declared producers for semantic classes are:

| Class | Decision | Checker definition and retained fact |
| --- | --- | --- |
| 1 | Expression type | The semantic checker writes `est::SemanticFacts::type` from its expression cursor at `src/semantic_checker.cpp:26116, 26399, 26508, 26677`; facts are keyed by expression and specialization context in `src/est/expression_tree.hpp:53–75`. |
| 2 | Call target | The checker records `SemanticFacts::resolved_target` and `Program::semantic_call_edges` during checked call resolution at `src/semantic_checker.cpp:14196–14226, 26420`; the fact fields are declared in `src/est/expression_tree.hpp:70–75` and `src/ast.hpp:739–755`. |
| 3 | Specialization | Checker specialization registration writes `Function::specializations` and call-edge argument types; consumers map those types through `checked_call_edge_specialization` at `src/ast.hpp:945–1002`. |
| 4 | Pipeline stage and output type | `SemanticChecker::add_functional_pipeline_ir` records `FunctionalPipeline::nodes` and each node's `kind`, `callable_identity`, and `output_type` at `src/semantic_checker.cpp:21548–21730`; the record is `src/functional_ir.hpp:128–145`. |
| 5 | Parameter effects and passing | Effect inference writes function and specialization parameter effects in `src/semantic_checker.cpp:839–871`; declarations are in `src/ast.hpp:435–438, 568–590`. These remain known downstream-consumer debt for this phase. |
| 6 | Clone, move, and borrow choices | The checker ownership pass runs at `src/semantic_checker.cpp:871, 10333–10344`; checked ownership records include `Function::parameter_effects` and `Stmt::ownership_consumed_bindings` at `src/ast.hpp:345, 435–438`. Emitter-local lowering choices remain known debt. |
| 7 | Control-flow joins | The checker stores the merged environment in `Stmt::joined_types` and `joined_types_by_context` at `src/semantic_checker.cpp:13037–13038`; the fields are declared at `src/ast.hpp:297–303`. |
| 8 | Pattern meaning | The checker resolves enum identity, variant, and bindings from `est::PatternNode` in `phase21_pattern_identity` and `phase21_pattern_bindings` at `src/semantic_checker.cpp:19675–19730`; the retained pattern structure is declared at `src/est/expression_tree.hpp:180–194` and attached at `src/ast.hpp:265–267, 356–357`. |
| 9 | Lock requirements | `SemanticChecker::build_synchronization_plan` constructs `Program::synchronization_plan` at `src/semantic_checker.cpp:17555–17630`; the program field is `src/ast.hpp:923`. |

The conflict registry for this round is [27.5.3 conflicts](phase27/27_5_3_CONFLICTS.md). It contains no entries at the current C1 implementation checkpoint. Phase 27.5.4 tracks its in-progress pipeline-precedence compatibility case in [27.5.4 conflicts](phase27/27_5_4_CONFLICTS.md). This is not evidence that all consumers have been migrated: classes 1–4 still have unmeasured consumers, and perturbation/removal tests have not yet established end-to-end authority.

The Fast Debug pipeline/handle slice was checkpointed in commit `88f611ce`. Its targeted tests covered `examples/functional_basics.moss`, `tests/phase27/est/specialized_call_facts.moss`, and `tests/swarm_001_parenthesized_pipeline.moss`; at that commit all three matched native plus optimized generated execution and reported zero Fast Debug reparses. Removing the final Fast Debug expression-tree parse fallback then caused 54 failures in the default 165-job suite, each sampled failure reporting `MOSS_INTERNAL_INVARIANT missing checked expression handle`. Representative accepted sources were `tests/phase106e_domains.moss`, `tests/tooling/fixtures/phase21_addendum_grouped_root.moss`, and `tests/tooling/fixtures/phase21_capture_owned_string.moss`; their prior Fast Debug behavior was restored. The broad fallback removal is therefore not part of the current candidate. The targeted parity gate remains registered; it does not claim zero reparses for the wider corpus.

The Phase 27.5.3 C1 attempt did not implement K2/K3 perturbation and removal tests for classes 1–4, 7, and 8, nor 100% class 1–4 consumer fact coverage. C1 is blocked. The exact full-suite run on the attempted fallback removal passed 111/165 jobs and failed 54; `MOSS_INTERNAL_INVARIANT missing checked expression handle` appeared in both recovery fixtures and the Fast Debug shard. After restoring the fallback, `make check` passed 165/165 jobs in default mode and 165/165 in shadow mode. The focused three-engine and `.mossi` gates passed as part of those suites. Compiler sources are identical to `origin/main`, and `build/compiler/moss-opt` has the same SHA256 as the saved phase27-5-2 baseline binary (`98c9cd8769b4a120c6154e6973f9f59dcf70a228fe9334f4b484ac366fb331f7`); no bulk performance suite was run, so timing and peak RSS are unmeasured.

The passing remainder was merged as a blocker checkpoint at `c96bca786caefa5744000af9918be50a65b41037`, first parent `b4670ca1e21dbcf0cf3e7a5413898fa1fd7f0bac`, and tagged `phase27-5-3-c1`. Agent 2's exact-SHA audit is summarized in [the Phase 27.5.3 report](phase27/27_5_3_REPORT.md).

## Phase 27.5.4 C1 fact-hook contract (in progress)

`tests/tooling/check_phase27_5_4_fact_hooks.py` is registered in `make check`
for default and shadow runs. It uses three accepted fixtures per class and
requires Fast Debug, native lowering, and `-O` to fail with
`MOSS_INTERNAL_INVARIANT` when the test build perturbs or drops a checked
answer. The expected behavior for these fixtures is fail-closed: the mutation
invalidates the checked answer, so successful execution/lowering is a test
failure. The hook implementation is guarded by `MOSS_TEST_BUILD` (`src/ast.hpp`
and `src/semantic_checker.cpp`); `moss-dev` defines it in `Makefile:18`.
 Agent 2 selected and ran an additional fixture per class during the C1 audit;
 paths and outcomes are listed in `docs/phase27/27_5_4_REPORT.md`.

The passing perturb/drop hook set is call targets, specializations, joins,
and patterns; it runs on Fast Debug, native emission, and `-O`. The registered
fixture counts are 5, 4, 4, and 3 respectively, including each C1 Agent 2
selected fixture. The expected result for both perturb and drop is
`MOSS_INTERNAL_INVARIANT` in every engine. No passing perturb/drop proof exists
for expression-type, pipeline-type, or ownership facts. Those remain explicit
known consumer debt:

| Decision | Fast Debug | Native emitter | Optimizer |
| --- | --- | --- | --- |
| Expression types | Runtime behavior ignores the perturbed answer on all three registered fixtures. | Ignores it on `side_facts.moss`; other fixture outcomes are in the baseline JSON. | Ignores it on `side_facts.moss`; other fixture outcomes are in the baseline JSON. |
| Pipeline stage/output types | Runtime behavior ignores it on all three fixtures. | Ignores it on `pipeline_shape.moss` and `collections_and_methods.moss`. | Ignores it on `collections_and_methods.moss`. |
| Ownership / clone-move-borrow | Runtime behavior ignores it on `phase4_effect_smoke.moss`. | Ignores it on `phase4_effect_smoke.moss`. | Ignores it on `phase4_effect_smoke.moss`. |

This table records known gaps and missing evidence, not a claim that each
engine ignores every fact in that class. K1 remains report-only in the current
re-scoped phase. C1's default and shadow suites pass 165/165, and Agent 2
returned GO on the candidate recorded in `docs/phase27/27_5_4_REPORT.md`.

## Phase 27.5.4 C2 delivery

The retained-tree emitter/optimizer C2 implementation is delivered at tag
`phase27-5-4-m1`, first-parented on the fetched `origin/main`. Fast Debug,
emitter, and optimizer consumer expression reparses and text-keyed node lookups
are zero by the C2 corpus counters and source audits; the emitter measurement
covers 1,824 tracked test/example files in default and `-O`, while package
manifests are inventoried but not run as package-context commands. The
string-based compatibility type inference in
`src/native_generator.cpp:3275–3609` remains reported debt.

The fact perturb/drop matrix passes for call targets, specializations, joins,
and patterns; expression types, pipeline types, and ownership retain the
38-entry explicit-probe ignore baseline. Default and shadow suites each pass
162/165; the three failures are the pinned-parent conformance gate 10 and the
two `swarm_035_nested_mutating_calls.moss` Rust E0499 jobs. The canonical W2
performance gate passes at 24,316 KiB peak RSS. Paired C1-parent performance is
+2.19% aggregate, W4 +2.55%, W5 +1.98%, W8 +0.22%; all 23 real-world workloads
remain within the established slowdown noise rule. Agent 2 returned GO for the
cost-repaired candidate. Full test and measurement details are in the
[Phase 27.5.4 report](phase27/27_5_4_REPORT.md).

`tests/tooling/check_phase27_5_4_fact_hooks.py` perturbs and drops these three
classes on three accepted fixtures each, then compares Fast Debug, native, and
optimized results with the unmodified run. The exact ignored
`(class, action, fixture, engine)` cases are stored in
`tests/tooling/fixtures/phase27_5_4_fact_hook_debt.json`; make check fails only
if that set grows. This is a bounded known-debt regression baseline, not proof
that untested consumers are migrated.

## Phase 27.5.4 closeout delivery

The post-C2 closeout is delivered at tag `phase27-5-4-m2`, on top of
`phase27-5-4-m1`. `make check` runs default mode; `make check-shadow` runs the
shadow cross-checks and is required at phase checkpoints and before merges to
main (AGENTS.md). The fixed runtime ratchet gates invariants 1–3; its workload
and per-stage ceilings are in [the workload](../tests/tooling/fixtures/phase27_5_4_runtime_workload.json)
and [the baseline](../tests/tooling/fixtures/phase27_5_4_runtime_baseline.json).

On that workload, checker counters are 54,037 reparses and 16,261,580
text-cache lookups; driver rewrites are 748 reparses and text-to-tree rebuilds.
Fast Debug, emitter and optimizer remain at zero for reparses, text-node/cache
lookups and rebuilds. Shadow cross-check counts are checker 64,548 and driver
rewrites 748, with zero hits in consumer stages. Shadow should retire only when
invariant 1 is met by the checker and invariant 2 is met by driver rewrites.

Both full suites ran once and each reported 162/165 jobs: conformance gate 10
(`compiled_only_custom_key`) and the `swarm_035_nested_mutating_calls.moss`
Rust E0499 lowering defect in two jobs. These are the documented C1-parent
baseline failures. Agent 2 audited the closeout candidate and returned GO with
those failures accepted as recorded debt. Details are in the
[Phase 27.5.4 report](phase27/27_5_4_REPORT.md).

## Phase 27.5.5 expression and pipeline type authority

The status above is historical. At Phase 27.5.5, expression and pipeline
types are no longer known consumer debt. The emitter's node-based
`generated_expr_type` reads a checked fact and fails closed when the answer is
missing (`src/native_generator.cpp:3138–3265`); the string-only overload throws
an internal invariant. Fast Debug also requires checked expression/stage type
answers (`src/interpreter.hpp:1203–1317`), and the optimizer reads the checked
pipeline plan (`src/functional_optimizer.cpp:640–688`). `MOSS_TEST_PERTURB` and
`MOSS_TEST_DROP_FACT` exercise these answers through Fast Debug, native and
optimized execution; only the 10 frozen ownership entries remain in
`tests/tooling/fixtures/phase27_5_4_fact_hook_debt.json`.

The final paired emitter sweep measured 1,828 tracked test/example sources in
default and `-O` (3,656 commands). It recorded emitter PRESENT=174,024,
ABSENT=0 and optimizer PRESENT=1, ABSENT=0, with zero consumer type inference,
expression reparses, functional-pipeline reparses and text-node lookups. Both
the Phase 27.5.3 baseline and candidate had 1,406 and 1,410 failed commands
(703 and 705 per mode), respectively. The two extra candidate-only sources
are accepted failure probes that now hit missing-fact invariants; the parent
emitted Rust that did not compile for these probes. The machine-readable result
is `tmp/phase2754-emitter-counts/1793619016465805546/summary.json` and the
durable phase report is [27.5.5](phase27/27_5_5_REPORT.md). This replaces the
earlier 23,452/282,668 (8.3%) coverage measurement above.

On the fixed 27.5.4 workload, checker reparses were 55,160 and checker
text-cache lookups 16,456,102, compared with prior measurements 54,037 and
16,261,580. The explicit 27.5.4 consumer-only re-scope makes these checker
counts report-only; the ratchet gates driver rewrites and consumer stages.
Those consumer stages remain zero and driver rewrite counts remain at baseline.

Paired performance against phase-start `274f8485` was 5.967s vs 15.029s
aggregate (-60.3%); against phase27-5-4-m2 it was 5.976s vs 5.789s (+3.2%).
The W2 n032 candidate peaked at 64,624 KiB using the normal per-translation
unit flags and 65,404 KiB with plain `-O2` everywhere. Full default/shadow
suite results and Agent 2 review are recorded in the report after final gates.

Rust-only lowering forms remain distinct from Moss type answers: effect
borrows and the `MossRead`/`MossWrite`/`MossAbsent` wrappers encode checked
parameter effects (`src/handler_lowering.inc:96–107`,
`src/handler_runtime.cpp:30–41`); `Result<_, __MossRaised>` transports raised
outcomes (`src/native_generator.cpp:677–725, 8717–8727`); and per-domain
`Arc<DomainRuntime>` plus `*Ref` wrappers implement runtime storage and
identity (`src/native_generator.cpp:9043–9044`). `MossStr`, `MossSet`,
`MossMap`, `Option`, and `Vec` remain representations of Moss value types.
