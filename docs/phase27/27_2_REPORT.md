# Phase 27.2 EST completion

## Review verdicts

- CP-C final frozen SHA `4ad2f84e2ef52538ddd86b672f1abe7433648ec2`: **“CP-C: GO”**. The fresh independent review found no unexplained verdict or checked-closure differences, no package-mode or user-visible query changes, and no sanitizer findings. The reviewer worktree was clean and unchanged. The unchanged report and probes are indexed at `docs/phase27/adversarial/CP-C-4ad2f84e.md` and `docs/phase27/adversarial/CP-C-ARTIFACTS.md`.
- CP-D candidate compiler source checkpoint `9db3e6e0` (binary SHA-256 `4a334c71d9b5ea5b2925c311f7a462abd046e479fc4b52df4c0453e612ceb286`), compared with the pinned `phase27-1-m1` binary (SHA-256 `4a9d7d7509f9883194f2d3f147c4ff66c39e9c832e758ddffdd7572d57667ffd`): **GO — 2,118 tracked sources; zero unadjudicated differences; zero generated-Rust differences in default and optimized modes; zero Fast Debug output differences.** The full machine report is `tmp/phase27-2/cp-d-9db3e6e0/report.json`; the harness and the mini-application diagnostic reproducer are committed under `tests/phase27/adversarial/cp-d-9db3e6e0/`.

## Adjudicated differences and corrections

- Five baseline checker SIGSEGVs on deferred generic pipelines are fixed by the EST path. The CP-D two-path corpus run records baseline exit `-11` and candidate off/shadow exit `0` for each. These are the only CP-D verdict differences and are allowlisted by exact source path in the harness: `examples/swarm/expression_surface_2026/validation_engine/failed_attempts/04_pipeline_collection_inference.moss`; `examples/swarm/modules/domain_services/repro/generic_pipeline/single/src/main.moss`; `examples/swarm/modules/domain_services/repro/generic_pipeline/split/src/main.moss`; `examples/swarm/modules/domain_services/repro/generic_pipeline/split/src/stats.moss`; `examples/swarm/modules/lib_and_app/repro/generic_pipeline_method_placeholder/single/src/main.moss`.
- The module-qualified `node.kind_num()` acceptance correction follows LANGUAGE_SYNTAX §Modules and C1: the legacy splitter selected unrelated `token.kind_num`, while the tree resolves `node.kind_num`. Package-mode checking still rejects the fixture with the same `UNKNOWN_SYMBOL_OR_TYPE` diagnostic on both paths. Reproducer: `tests/tooling/check_phase27_reach_named_forms.py`.
- Eight legacy-only checked declarations / 42 reach edges are qualified-module same-leaf splitter artifacts; the per-source two-path harness records checked sets and verdicts. The two current tree-only receiver candidates (`Ast.size`, `Env.size`) preserve the required receiver-name over-approximation. Initial 13 declaration audit was corrected: nine unsupported chains were removed; only four audited chains were concrete, and subsequent CP-C found no unexpected omissions.
- All 7,599 reach-edge differences are accounted for: same-leaf spelling 6,102 legacy-only / 283 tree-only; legacy keywords and placeholders 302/0; enum and pattern labels 66/0; shadowed locals or parameters 120/0; built-in constructor nodes 0/680; qualified-module legacy misparse 42/0; receiver-name candidate over-approximation 2/2. The classification examples and non-benign two-path reproducers are recorded in `docs/phase27/adversarial/CP-C-4ad2f84e.md`. No user-visible `moss calls`, `moss impact`, or fingerprint change was found.
- The Fast Debug unsupported pipeline stage `schedule_score` diagnostic mismatch was fixed by preserving the legacy runtime error through EST dispatch. The reproducer compares exact exit code, stdout, and stderr across baseline, candidate off, and candidate shadow: `tests/phase27/adversarial/cp-d-9db3e6e0/check_pipeline_error_parity.py`.
- CP-D found three 120-second Fast Debug timeouts, identical across baseline/off/shadow: `benchmarks/compiler_torture/fixtures/examples/functional_pipeline.moss`, `examples/frontend_syntax.moss`, and `examples/functional_pipeline.moss`. Among completed executions, 855 returned exit 0 and 74 returned exit 1 on each of the three paths, with exact output parity.

## String debt and remaining 27.3 work

The widened lint passes. Current reader-site totals / frozen widened baseline / net change: reach walk 0/52/-52; checker 454/467/-13; Rust emitter 124/128/-4; Fast Debug 0/129/-129; optimizer 9/9/0. The Fast Debug strict zero-debt gate passes. Functional-pipeline parsing is tree-backed in the checker, and the emitter no longer parses pipeline text. Remaining optimizer-owned text reads are 27.3 scope:

- `src/functional_optimizer.cpp:608` `split_top_level`; `:609`, `:787`, `:933` trim comparisons.
- `src/functional_optimizer.cpp:811`, `:1055` `plain_identifier`.
- `src/functional_optimizer.cpp:848`, `:856`, `:1098` `word_occurrences`.

## Gates

| Gate | Result | Evidence |
|---|---|---|
| CP-D corpus parity | 2,118 sources; zero unadjudicated differences; zero Rust/Fast Debug differences | `tmp/phase27-2/cp-d-9db3e6e0/report.json`, `tmp/phase27-2/gates/cp-d-9db3e6e0.log` |
| ASan/UBSan shadow corpus | 2,118 sources; zero findings | `tmp/phase27-2/gates/sanitized-corpus-final.json`, `.log` |
| `make check`, EST off | 159/159; 0 failures/skips | `tmp/phase27-2/gates/check-off-final-checkpoint.log` |
| `make check`, EST shadow | 159/159; 0 failures/skips | `tmp/phase27-2/gates/check-shadow-final-checkpoint.log` |
| `.mossi` baseline parity | normalized Section 1/2 equality; execution body hashes equal | `tmp/phase27-mossi-baseline/1793339238261951943/report.json` |
| Feedback record validation | pass | `python3 tests/tooling/check_agent_feedback.py` |

Compiler performance was measured against the merge's first parent `a4f4790f1434197a7cd995644bbf5d7ebce3fb0f` in two 3-repetition runs. First run aggregate: 4.974 s baseline / 5.030 s candidate (+1.1%); beyond-noise workloads `w4_infer_deep_d015` and `w3_spec_repeated_n008`. Repeat aggregate: 4.963 s / 5.023 s (+1.2%); `w5_infer_wide_w050` remained beyond the noise floor at +6.0% (+3.45 ms; the baseline/candidate sample ranges did not overlap). Logs and reports: `tmp/phase27-2/gates/compiler-perf-merge-first.log`, `tmp/compiler-perf/20261029-230039/LOCAL_REPORT.md`, `tmp/phase27-2/gates/compiler-perf-merge-repeat.log`, and `tmp/compiler-perf/20261029-230230/LOCAL_REPORT.md`. This repeat triggers the standing performance escalation. The candidate is not pushed to `main`, and the release tag is not created pending resolution.

## Merge delivery

The local merge candidate has first parent `a4f4790f1434197a7cd995644bbf5d7ebce3fb0f`, the fetched `origin/main` head. Its full off and shadow suites passed 159/159 each before the performance escalation. It is pushed on `phase27-2-merge` for review; it is not integrated into `main` and has no `phase27-2-m1` tag.
