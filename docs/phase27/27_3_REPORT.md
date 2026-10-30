# Phase 27.3 report (in progress)

## Corrections and independent reproductions

| Classification | Finding and correction | Oracle | Reproducer |
| --- | --- | --- | --- |
| MIGRATION BUG | Checker effect/ownership walks split on the final dot in FileIO path strings, passing truncated fragments to recursive expression checking. Both now project EST `Member` nodes. | Phase 27.2 compiler at `04be1a1`; current off and shadow produce the same successful check result. | `tests/tooling/fixtures/phase20_fileio_runtime_integration.moss:29`; CP-E paired dotted-member probes. |
| MIGRATION BUG | `inferred_expr_core` split a member receiver at the final dot in the string argument `"tmp/phase21c-concurrent-a.data"`. It now projects member access from the EST node. | Phase 27.2 compiler at `04be1a1`; current off and shadow agree on successful checking. | `tests/tooling/fixtures/phase21c_concurrent_roots.moss:57`; calls `a.Run("tmp/phase21c-concurrent-a.data", 0, 1)`. |
| MIGRATION BUG | The `project_enum_case` shadow oracle parsed parenthesized case source without first removing redundant grouping, while EST projects the underlying constructor node. Shadow parity now invokes the unchanged legacy parser on the normalized child source. | `tests/phase27/adversarial/cp-e-fresh/check_cp_e_fresh.py` compares Phase 27.2 off, candidate off, and candidate shadow; all check and execution results agree for `unwrap((Choice.Value(value: 7)))`. | The parenthesized enum constructor case in that committed CP-E probe. |
| LEGACY MISPARSE | `split_binary` sees `==` inside the rejected statement-shaped expression `if ...: ... else: ...`; EST correctly has no expression root. Shadow comparison narrowly accepts only this `if`/colon/`else:` shape. | Phase 27.2 off, candidate off, and candidate shadow report the same `INVALID_EXPRESSION`; the independent oracle is the checker diagnostic. | `invalid_if_expression` in `tests/phase27/adversarial/cp-e-fresh/check_cp_e_fresh.py`; also `x = if 1 == 1: 1 else: 2` in `tests/tooling/check_phase25_s2_char_string.py`. |
| MIGRATION BUG | Fresh CP-E found raw punctuation and prefix tests on assignment places and vector initializers, plus a source-text search for a member-call requirement origin. These now use EST index/member/vector projections and a node walk for matching member calls. FileIO call reconstruction also uses an identifier projection. | Phase 27.2-m1, candidate off, and candidate shadow agree on the indexed/member assignment runtime fixture; the imported-generic and typed semantic API diagnostics pass. | `tests/indexed_field_assignment.moss`; CP-E's four unchanged source probes; `tests/tooling/check_phase26_1_t1_generic_imports.py`; `tests/tooling/check_phase25_typed_semantic_api.py`. |

Focused evidence from the current tree: `tmp/phase27-3/cp-e-after-enum-and-if-fix.json`, `tmp/phase27-3/phase21c-phase27-2.json`, and `tmp/phase27-3/fileio-phase27-2.json`. Full gates at pushed `53025f4d` are recorded in `.codex/CURRENT_STATUS.md`; they predate the fixes in this report and must be rerun at a new checkpoint.

## Remaining sections

## Fresh CP-E checkpoint

Agent 2 reviewed frozen candidate `068eb9aa130e51778441f222b1a449a48247bef9` against phase27-2-m1 `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4` and returned **CP-E verdict: NO-GO**. Evidence commit: `88b2b006f8c97adfd3f1e71e0bb7421eb01bb249` (recorded on the phase branch as `c5741773`). Four independent probes matched verdicts and diagnostics in baseline, candidate off, and candidate shadow. The review found expression-shape reads missed by the widened lint: bracket/dot checks on assignment targets; bracket-prefixed initializer checks for vector classification in checker and emitter; and a dot/call-suffix assignment-target classification. Full inventory and exact locations: `docs/phase27/adversarial/cp_e_agent2_068eb9aa_inventory.tsv`; report: `docs/phase27/adversarial/cp_e_agent2_068eb9aa_review.md`; probes: `tests/phase27/adversarial/cp-e-agent2-068eb9aa/`. These are unconverted migration reads; no semantic delta was reported. The exhaustive inventory and full corpus closure differential are still open.

The off-mode full gate was started at `068eb9aa` and interrupted after the CP-E NO-GO, so there is no full-gate result. Shadow full gate was not started on that checkpoint. After the raw-shape fixes, `make moss-dev`, the EST focused harness, Phase 25 S2 char/string suite, shadow Phase 20 FileIO semantics, four unchanged CP-E probes against baseline/off/shadow, indexed/member assignment runtime output against baseline/off/shadow, imported-generic checks, typed semantic API checks, all four zero-reader lint invocations, and feedback validation pass. Fresh exhaustive CP-E is still pending on a pushed checkpoint. CP-F, semantic-fact audit, full parity/sanitizer/performance gates, merge SHA, and tag remain outstanding.

Full off gate at `6b926fc7`: **FAIL**, 157/159 jobs passed, zero skips; log `tmp/phase27-3/check-off-6b926fc7.log`. `check_run_parallel_timeout` hit a `ProcessLookupError` while reading the child process stat; `check_phase25_4_performance_baseline` exceeded w2 (1216 ms against 1150 ms) and w3 (946 ms against 900 ms). CP-E was concurrently running a full corpus differential, so repeat the performance-sensitive gate without that load. No waiver or final performance disposition is made.

## Escalation blocker — semantic/package verdict change

CP-E verdict: NO-GO

Frozen candidate: `6b926fc7dd3ed42dedddb0f2f8f60f931b106a68`

Baseline `phase27-2-m1`: `d4446b7250f651e60e978740661e6fb20ff6ef10`

Evidence payload: `c8cb5ae60e5f1017dfcebb56f97256ca6e6160ff` (review branch HEAD `aa0dc86ea4751df5e5cbe901bed0e9b3841e3559`; payload and follow-up are recorded on this branch as `8687e1a2` and `21e74a74`).

On `tests/phase27/est/repro/incomplete_expression.moss` and the identical benchmark source `fn main(): echo 2 +`, baseline off rejects with `MOSS_COMPILE_ERROR:2`, candidate off accepts, and candidate shadow aborts with SIGABRT before JSON. A paired mini-package `margo build --check` reproduces baseline rejection and candidate acceptance. **Classification: SEMANTIC**, and a package-mode verdict changed. The source-free SDK fixture also changes standalone diagnostic from `RECOVERY_PATTERN_REQUIRES_VARIANT:57` to `UNKNOWN_ERROR_TYPE:57`; its actual provider-interface package result is unverified. Exact per-file evidence is in `docs/phase27/adversarial/cp_e_rawshape_6b926fc7_verdict_diff.tsv`, paired/package tables, report, inventory, and probes under `tests/phase27/adversarial/cp-e-rawshape-rerun/`.

The exhaustive field inventory contains 1,192 line-level occurrences: 113 EST/expression-reader calls, 96 raw-string-operation review hits, 58 literal-comparison review hits, 484 expression-value uses, and 441 mixed name/expression uses. `tree_expression_text` has zero checker/emitter uses; there are 39 audited NAME comparisons, 9 binding comparisons, and three canonical-type split exemptions. CP-E demonstrates the widened lint still has an alias/helper blind spot: it catches direct Stmt-field shape tests but misses equivalent tests after copying expression text to a local string or helper parameter. Remaining indirect raw readers are enumerated and classified in the committed review.

The off verdict-corpus comparison completed: baseline had 2,118 sources and 1,394 accepted; candidate had 2,124 sources and 1,398 accepted; 2,118 paths were common, with three verdict changes and six candidate-only sources. The checked-declaration closure differential timed out at 90 seconds on a specialization workload; its 300-second retry was stopped before producing a report. No closure pass is claimed.

## L7 ruling and correction — incomplete expression

L7 ruled that acceptance of malformed REJECTED-SOURCE input is a MIGRATION BUG, not SEMANTIC. The required behavior is identical diagnostics on both paths. The incomplete-expression mismatch is therefore a fix-and-rerun finding, not a stop condition.

The EST incomplete-trailing-operator query now identifies incomplete operator tails. Checker validation emits the baseline MOSS_COMPILE_ERROR / invalid arithmetic expression before semantic interpretation; shadow binary-split parity avoids asserting on the legacy partial pair for that rejected input. Regression coverage in tests/tooling/check_phase27_est.py checks the 27.1 rejected-source fixtures for **, in, &&, ||, incomplete expressions, and all six files containing the seven isolated delimiter fragments. It requires the baseline diagnostic signature in off and shadow. The incomplete-expression repro is also checked in standalone and package modes. The focused EST regression passes.


## Latest checkpoint — 2026-10-30

The preceding review sections describe historical frozen checkpoints. The current Phase 27.3 branch is `phase27-3-work`, pushed through `175c7592`; the source checkpoint reviewed by CP-F is `ea7bf9f47cca36f5815481a683bbda3d7e766631` (CP-F freeze `2bafad47` includes only evidence/status commits afterward).

### Final CP-E review

- Fresh CP-E at `c211da42` returned `CP-E verdict: NO-GO` because `observable_body_effects` copied `For.b` into `iter_expr` then classified `range(...)` and vector shapes with raw prefix/suffix checks. This is a MIGRATION BUG under L7. Correction: checker now uses EST simple-call and vector-literal projections in `semantic_checker.cpp` (fix `ea7bf9f4`). Oracle: Phase 27.2-m1 compiler `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`; candidate off and shadow match it. Reproducer: `tests/phase27/adversarial/cp-e-phase27-3-rereview-for-alias.moss`.
- Fresh re-review at `ea7bf9f47cca36f5815481a683bbda3d7e766631` returned `CP-E verdict: GO`; evidence commit `6b43550294a206891fda449b3590538df47d0322` is recorded on this branch as `2bafad47`. It found parity across 934 `.moss` sources. Checked-declaration closure accounts for 537 accepted sources; the 397 rejected sources are explicitly listed as exclusions with diagnostics. Callable census: 1,322 reachable and 163 unreachable. Parenthesized enum, recovery, malformed-source, and for-alias probes matched. Package build matched; package test failed identically with `MODULE_PROVIDER_NOT_FOUND`. Full report and raw evidence: `docs/phase27/adversarial/CP_E_PHASE27_3_REREVIEW.md` and adjacent CP-E JSONL files.
- Fresh CP-E independently confirmed 40 literal NAME comparisons and 9 binding-name comparisons, three function/API-keyed type-argument exemptions, zero `tree_expression_text` source reads, and zero checker/emitter reader sites. The regex lint still missed the `For.b` alias; a bounded taint audit caught it. That lint blind spot and its resolution are recorded in `agent-feedback/phase27/phase27-3-expression-tree/checker-reader-audit/observations.jsonl`.

### Emitter semantic-fact audit (record only; no moves in 27.3)

- Parameter ABI mode: `src/native_generator.cpp:1223` — `function_parameter_abi` chooses value/shared-borrow/mutable-borrow using parameter effects, concrete type, and generic constraint operations. Specialization facts take a separate path through `concrete_parameter_abi`.
- Synchronization fragment locks: `src/handler_lowering.inc:146` — `path_fragment_plan` rebuilds a fragment lock set from writes, consumes, and protected reads, then maps leaves to classes/modes from the domain plan.
- Domain construction ordering: `src/native_generator.cpp:5381` and `src/native_generator.cpp:5393` — the emitter separates constructor statements from executable statements and orders constructors by reverse domain rank; `src/native_generator.cpp:6967` recognizes constructor expressions.
- Functional lowering choice: `src/native_generator.cpp:3117` — the emitter dispatches to fused or eager code generation from `planned->fused`; nested unplanned pipelines use eager lowering. This consumes checked/optimized plan fields but still selects the backend path.

These decisions remain for a later backend-contract phase; Phase 27.3 did not move them.

### CP-F and remaining gates

CP-F completed at frozen candidate `2bafad47` against baseline `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`. Its exact verdict is **CP-F verdict: NO-GO**. The valid-source overflow finding is awaiting L7 classification; details and raw evidence are in `docs/phase27/adversarial/CP_F_PHASE27_3_REPORT.md` and `tests/phase27/adversarial/cp-f-phase27-3/`. Full differential, sanitizer, and shadow findings are recorded there. Idle-machine full gates, conformance, final sanitizer disposition, compiler-perf vs the merge first parent and `phase27-1-m1`, merge, push, and tag remain outstanding. No Phase 27.3 completion is claimed.

### CP-F shadow assertion follow-up

CP-F exposed two `project_simple_call` shadow assertions on `nested-stage.moss` and `stage-flat.moss`. In both, the EST root is a multi-stage `Pipeline`, so it has no simple-call projection; the legacy trailing-call scan nevertheless returns a call from a nested stage. The shadow check now treats a missing simple-call projection on a non-Call/Constructor/Message root as that legacy false positive. This leaves off-mode and shadow-mode semantics unchanged.

Oracle and two-path reproducer: CP-F's committed probe sources `tests/phase27/adversarial/cp-ca9e8ca/nested-stage.moss` and `stage-flat.moss`; phase27-2-m1, candidate off, and candidate shadow return identical diagnostics (`TYPE_AMBIGUOUS_OPEN_RESULT` and `UNKNOWN_SYMBOL_OR_TYPE`, respectively). The EST root kind independently establishes that neither expression is a simple call. Classification: LEGACY MISPARSE. Focused build and all three-path JSON comparisons pass. This correction is checkpointed at `1707a0cc` with the CP-F evidence import.


### CP-F pipeline/comparison shadow follow-up

Five CP-F shadow assertions came from rejected source forms where an unparenthesized comparison-looking suffix follows a pipeline. The EST root is `Pipeline`; the legacy right-to-left splitter reports a top-level comparison anyway. Both Phase 27.2-m1 and candidate off reject each exact source with `UNKNOWN_SYMBOL_OR_TYPE` (`unknown identifier 'count'`, `unknown local function 'count'`, or `unknown identifier 'sum'`); candidate shadow now returns the same diagnostic instead of aborting. The correction is limited to a legacy split when the complete EST root is a `Pipeline` and no tree binary projection exists.

Classification: **LEGACY MISPARSE**. Independent oracle: both engines reject the unparenthesized stage form with the same unknown-stage diagnostic, while parenthesizing the pipeline before comparison is accepted (`(values |> size) > 0` in the minimized user-function probe). Two-path reproducers are the five exact CP-F files listed in `tests/phase27/adversarial/cp-f-phase27-3/shadow-assertions.json`; the candidate off/shadow diagnostic pair is asserted by `tests/tooling/check_phase27_est.py`. All five candidate shadow checks return their expected JSON diagnostics. The change is at the current continuation checkpoint; no accepted program behavior changed.

### CP-F source-free standalone diagnostic delta

The standalone `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss` differs at line 57: phase27-2-m1 reports `RECOVERY_PATTERN_REQUIRES_VARIANT` for `ndarray.NdArrayError.InvalidAxis`, while candidate off reports `UNKNOWN_ERROR_TYPE` because the standalone fixture has no imported provider interface declarations. Classification: **LEGACY MISPARSE**. The baseline pattern reader accepts at most one dot in a qualified recovery case and therefore misclassifies a module-qualified enum case as malformed. The independent oracle is the intended ABI-v9 source-free package gate, which supplies 14 provider interfaces and 5 modules: baseline and candidate both build and execute the consumer successfully with the same output. Two-path reproducer: the exact standalone fixture above checked by baseline `04be1a1` and candidate off at frozen CP-F; package oracle logs are `tests/phase27/adversarial/cp-f-phase27-3/logs/source-free-{baseline,candidate}.log`. The diagnostic delta is adjudicated; it is not a package-mode verdict or behavior change.


### CP-F valid-source overflow ruling and idle rerun

L7 classified the `maximum + 1` argument to `message sink.Identity(...)` as **MIGRATION BUG**. Oracle: the Moss v0.1 guide documents wrapping for the `Int`-minimum division overflow case, and phase27-2-m1 emits wrapping arithmetic for all checked Int binary operations; Fast Debug and baseline native default/`-O` on the probe all wrap to `-9223372036854775808`. Reproducer: `tests/phase27/adversarial/cp-f-phase27-3/wrapping-message-argument.moss`. Candidate native lowering must restore wrapping behavior in this root-ingress argument.

An idle isolated rerun of the CP-F n032 shadow timeout fixtures at 120 seconds found: baseline w2 7.987s, candidate off w2 28.081s, candidate shadow w2 timeout at 120s; baseline w3 4.556s, candidate off w3 20.432s, candidate shadow w3 completed in 91.727s. Captures are under `tmp/phase27-3/cpf-timeout-followup/`. The w2 shadow timeout is a real remaining gate failure; no waiver is claimed.


### CP-F message-argument wrapping correction

The root-ingress message argument `maximum + 1` exposed an EST view provenance bug. `expression_view_from_est` matched a retained node by text fingerprint alone, then paired a nested node's parent-relative source span with the shorter child string. `project_binary` consequently failed to identify the `+` root, and the emitter fell through to plain Rust `+`. The emitter now reuses a retained handle only when its root span covers the complete supplied text from byte zero; otherwise it reparses the child expression into EST. This restores the wrapping Int lowering.

Classification: **MIGRATION BUG**. Oracle: Moss Int operations wrap (the v0.1 practical guide documents wrapping for the Int-min division boundary; phase27-2-m1 emits `.wrapping_add` for this checked Int operation). Reproducer: `tests/phase27/adversarial/cp-f-phase27-3/wrapping-message-argument.moss`. Baseline, candidate off, and candidate shadow Fast Debug, native default, and native `-O` all print `-9223372036854775808`. Build, focused EST test, checker/emitter/reach-walk/Fast Debug reader lint, and `git diff --check` pass. This source correction is not yet re-reviewed by CP-F.

### Profiled w2/w3 cache follow-up

Three idle candidate-off repeats against baseline measured the n032 fixtures: w2 medians were 28.308s candidate vs 7.820s baseline (3.62x); w3 medians were 19.771s vs 4.395s (4.50x). Shadow w2 with compiler profiling took 118.562s vs 27.713s off (4.28x); CP-F confirms candidate-only sanitizer shadow timeouts on both n016/n032 w2/w3 fixtures and five other ordinary inputs. The latest independent CP-F verdict is **NO-GO**, evidence commit `eec028321acbeded73495811b758088f455092e8` (imported as `a46d25a9`).

The W1 parse-cache profile showed 46,231 binary queries after its 32,768-entry cap; only 18,425 `(expression, operator-set)` keys were unique, with 27,806 repeats across specialization checks. Added a separately bounded 32,768-entry binary overflow cache. The w2 profile now records 18,425 overflow entries and 27,806 hits, eliminating those repeat comparisons. It did not materially fix total time: candidate off is still about 26.9s and shadow about 116.5s. Next profile the remaining `specialization_checking` shadow cost; do not increase the gate timeout. Build, focused EST checks, reader lint, and w2 off/shadow profile runs pass. Profile captures are under `tmp/phase27-3/profile-w2/` and `tmp/phase27-3/w2-*-binary-cache.*`.

### Memoized checker EST views and idle w2/w3 timings

Profiling the n032 specializations showed why shadow exceeded the parity budget: w2 requested `parse_view` 4,674,253 times for only 4,379 distinct source strings, all within the 8,192-entry bound. The repeated work was parse/projection lookup per specialization, not expensive legacy comparison: binary reader queries totaled 3,598,987, with 3,554,359 hits and 44,628 misses; accumulated legacy split time was about 33ms. Added a per-check, bounded syntax-view cache and memoized the named-argument subview. Cache entries own immutable views so repeated hits do not copy their source text. Profile captures and repeat JSON are in `tmp/phase27-3/est-view-cache-repeats/`.

Three idle candidate-off repeats before view memoization measured w2 28.308s vs 7.820s baseline and w3 19.771s vs 4.395s. After memoization, idle repeats measured w2 baseline 7.614s / candidate 8.215s and w3 baseline 4.388s / candidate 4.728s (medians; candidate is +7.9% and +7.7%). This remains beyond the noise floor and the off perf gate is **FAIL**. Latest single shadow runs completed in 9.922s (w2) and 5.872s (w3), 1.21x and 1.24x the corresponding candidate-off medians; these are no longer shadow timeouts, but full sanitizer/corpus confirmation is outstanding. The view cache build, focused EST harness, string-debt gate, and diff check pass. Continue profiling and fixing the off regression; do not raise timeouts.

### Cached member/index projections

The checker hotspot report shows 1,091 ownership checks and 1,058 distinct concrete-effect inputs during w2 specialization. Member/index projections recur across those contexts, so the bounded EST view entries now memoize the two projection results, and ownership/effect paths use the same projection helper. The profile records 1,167,941 repeated projection hits against 4,376 misses. Three idle off repeats after this change measured w2 8.533s candidate vs 7.846s baseline (+8.8%) and w3 4.865s vs 4.597s (+5.8%). This does not clear the performance gate; the projection cache stays only as an independently useful memoization and needs further profile work. Build, focused EST, string-debt, and diff checks pass. Timing/profile artifacts are in `tmp/phase27-3/projection-repeats/`, `tmp/phase27-3/w2-projection-profile.stderr`, and `tmp/phase27-3/w2-hotspots-{baseline,}.stderr`.

### Explicit off performance gate after projection caching

Ran `MOSS_EST=off python3 tests/tooling/check_phase25_4_performance_baseline.py ./moss` idle at the current checkpoint. It fails w2 n016 at 2,066.77ms and 25,292 KiB (historical B1 limit 1,150ms / 25,000KiB), and w3 n016 at 1,328.92ms (limit 900ms); w4-w8 and the remaining cohort pass in this run. The script result is `tmp/phase27-3/perf-gate-off-cache-key-memory.log`. The requested direct idle comparison against phase27-2 baseline binary used three alternating repeats: w2 baseline median 1.859s vs candidate-off 1.988s (+6.9%); w3 baseline 1.220s vs candidate-off 1.297s (+6.3%). Both exceed the expected noise floor. Projection memoization and avoiding duplicate cached source keys did not clear the regression; performance remains an open blocker.
