# Phase 27.5 Fast Debug fixes and fail-closed guards — in progress

## D1 — duplicate Fast Debug `For`

- Removed the first duplicate `For` branch and retained one handler with block scoping, per-iteration `LocalWrite`, and native-style range termination. Added runtime support for checked custom Iterator sources using their checked `iter()`/`next()` methods.
- Targeted fixture `tests/tooling/fixtures/phase27_5_for.moss` covers range, Vector, Map key snapshots, and a custom Iterator. `moss check` accepts the fixture. Native Rust compiles and runs. Fast Debug output and traced Fast Debug output match native output (`1`, `2`, `4`, `5`, `1`); trace includes `LocalWrite` for `item`.
- Direct Set, Map, and Queue iteration is not accepted by the checker at this base; no new iteration semantics were added for them, and the unreachable Queue branch was removed.
- `python3 tests/tooling/check_phase27_5_for_match.py ./moss` passed. The fixture exercises accepted range, Vector, Map key snapshot, and custom Iterator forms; direct Set iteration is rejected with `TYPE_INFERENCE_FAILED`, direct Map iteration with `MOSS_COMPILE_ERROR`, and direct Queue iteration with `MOSS_COMPILE_ERROR`. The unreachable Queue path was removed from the Fast Debug handler.

## D2 — match payload Copy classification

- Both interpreter and emitter now use `copy_type_name()` for non-consuming payloads. The fixture covers Char, `option[Int]`, `option[Char]`, and String control payloads. Fast Debug and native output match; generated Rust compiles; the three Copy cases lower as dereferences and String is not cloned.
- `python3 tests/tooling/check_phase27_5_match_copy.py ./moss` passed. Registered separately in `tests/run_parallel.py` for off/shadow `make check`. D2 was committed and pushed as `d21499f6` to `origin/phase27-5-work`.

## Guards G1–G4 — combined bulk measurement and disposition

Guard code/report checkpoint: commit `42e934bf` pushed to `origin/phase27-5-work`.

- Combined no-hook pass: 1,808 `.moss` files under `tests/` and `examples/` were checked; accepted files were emitted, and accepted files containing `fn main` were run through Fast Debug. All 336 package manifests under those trees were built and Fast Debug run. The pass took 339 seconds. The command driver is `tmp/phase27_5_bulk_measure.py` (disposable local evidence). No performance suite was run.
- Natural guard hits: G1 183 (corpus 96, examples 63, packages 24); G2 0; G3 12 (corpus 8, examples 2, packages 2); G4 2 (corpus 1, examples 1, packages 0). Per phase rule, G1, G3, and G4 are REVERTED to their original fallback behavior with debug-build counters (`MOSS_TEST_COUNT_FACT`). G2 is KEPT fail-closed with the `MOSS_TEST_DROP_FACT=g2` hook and registered removal test. The removal test passes normally and fails when invoked with `MOSS_TEST_DISABLE_GUARD_HOOKS=1` (hook-off spot-check).
- G1 classification: 183 `FACT-PRESENT-UNUSED`, 0 `FACT-ABSENT`. Every firing enters the static-dispatch specialization fallback, whose target has checker-produced specialization entries. Sample classifications: `tests/duck_typed_methods.moss` — FACT-PRESENT-UNUSED (`describe`, Circle/Rectangle edges); `tests/generic_method_effects.moss` — FACT-PRESENT-UNUSED (`update` edge); `tests/phase224_debug_specialization.moss` — FACT-PRESENT-UNUSED (`identity`, Int/Bool edges). The matching `moss calls` output includes exact specialization keys. Across the 183 firing source runs, class totals are FACT-PRESENT-UNUSED 183 and FACT-ABSENT 0. The emitter fallback failed to consume the exact call-edge selection fact.
- G2 classification: 0 `FACT-PRESENT-UNUSED`, 0 `FACT-ABSENT`; no natural firing/sample.
- G3 firing samples: `tests/phase27/est/pipeline_shape.moss`, `tests/phase27/adversarial/cp-ca9e8ca/assoc-grouped.moss`, and `examples/swarm/Surface/repros/swarm_049a_fast_debug_map_in_method.moss`.
- G4 firing samples: `tests/phase47_iteration.moss` and `examples/swarm/Surface/repros/swarm_059_untyped_for_param_write.moss` (two firings total).
- Bulk counts include unrelated failures: corpus check 464/1032, emit 96/568, Fast Debug 73/462; examples check 225/776, emit 74/551, Fast Debug 6/292; packages build 95/336, Fast Debug 111/336. These are measurement results, not final suite results.

## Final branch gates — frozen implementation `20aaef22`

- `MOSS_EST=off make --jobs="$(nproc)" check` — PASS, 163/163 jobs. `MOSS_EST=shadow make --jobs="$(nproc)" check` — PASS, 163/163 jobs. Both include D1, D2, and G2 removal coverage.
- Explicit D1 and D2 fixture drivers passed again after the suites. `check_phase27_three_engine.py build/compiler/moss-dev` passed with identical output and callback count 3.
- First-parent compiler performance used `293bb6c53988c099974b04a0a21c5df77e9ef77e`, the phase base, for 77 fixtures, three interleaved repetitions, and one separate profiling sample. After fetching, current `origin/main` is `394e32d41ceb6a3034a5b8e7bde1bf10a84dcb4d`; the diff from the measured baseline contains only documentation, `.codex` status, and `AGENTS.md` changes, with no compiler, benchmark harness, or fixture changes. Thus the benchmark's compiler and fixture inputs are identical to the expected merge first parent. Aggregate median: 5.671 s → 5.726 s (+1.0%). No `rw_*` workload crossed the alert threshold. One torture fixture, `w6_types_count_t050`, crossed the report's beyond-noise threshold (+8.4%, +3.32 ms); other torture fixtures are report-only under the Phase 27 standing ruling. Full report and raw files: `tmp/compiler-perf/20261031-131846/LOCAL_REPORT.md` and adjacent CSVs. A preliminary run against `HEAD^` resolved to the intermediate guard checkpoint `42e934bf`; it is not used as first-parent evidence.
- Agent 2 returned **GO** on frozen SHA `20aaef22bb5c6ad2c38a42b58331307dfcb13997`. It confirmed G2 fallback deletion, original G1/G3/G4 behavior, D1/D2 invariants and registrations, and the hook-off removal test failed as expected. Review record: `docs/phase27/adversarial/phase27-5-agent2-audit.md`.

## Agent 2 adversarial review

**GO — reviewed implementation `20aaef22bb5c6ad2c38a42b58331307dfcb13997`.** The reviewer inspected that exact `HEAD` with a clean worktree. G2's Assign and Let/Var binding paths fail with `MOSS_INTERNAL_INVARIANT` and the type reconstruction fallbacks are deleted; the `g2` hook drops the checked type. The required `MOSS_TEST_DISABLE_GUARD_HOOKS=1` spot-check exited 1 with `G2 dropped fact did not fail closed`, as expected. G1, G3, and G4 retain their original fallback behavior with only counters and the G4 detection helper. D1's single handler, scoping, traces, fixture and registrations, and D2's shared Copy classification and fixtures were confirmed. No review blocker was found. The reviewer did not rerun the full suites.

## Delivery

- Implementation: `20aaef22bb5c6ad2c38a42b58331307dfcb13997` on `phase27-5-work`.
- Full off/shadow checks: 163/163 jobs each. D1/D2 native and Fast Debug parity, generated Rust compilation, trace assertion, and three-engine check passed.
- Agent 2: GO on the frozen implementation SHA above.
- First-parent performance: measured compiler/fixture baseline `293bb6c53988c099974b04a0a21c5df77e9ef77e`, source-equivalent to fetched merge first parent `394e32d41ceb6a3034a5b8e7bde1bf10a84dcb4d`; aggregate +1.0%; no real-world alert; one report-only torture alert `w6_types_count_t050` (+8.4%, +3.32 ms). See the full report path above.
- Merge: `aa694ac074678cafe7a052a8af186e69303eb435`; first parent: `394e32d41ceb6a3034a5b8e7bde1bf10a84dcb4d`; second parent: `46c12c969c271672fe2a58670b237fbe67751f81`.
- Annotated tag: `phase27-5-m1` targets merge commit `aa694ac074678cafe7a052a8af186e69303eb435`.
- The merge tree includes the reviewed implementation, the Agent 2 audit, the phase report, and the team closeout. Its compiler and fixture inputs match the frozen implementation SHA; documentation records the fetched-main baseline equivalence and performance disposition.
