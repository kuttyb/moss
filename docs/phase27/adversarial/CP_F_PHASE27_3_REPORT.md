# CP-F Phase 27.3

**CP-F verdict: NO-GO**

Frozen candidate: `2bafad47eb15af1923fbf1673b1a6069dca0f63e`  
Candidate compiler-source commit: `ea7bf9f47cca36f5815481a683bbda3d7e766631`  
Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`  
Evidence branch: `review/cp-f-phase27-3`

The valid source probe in `tests/phase27/adversarial/cp-f-phase27-3/wrapping-message-argument.moss` is a native behavior regression. Baseline Fast Debug, default Rust, and Moss `-O` print `-9223372036854775808`; candidate Fast Debug prints the same, but both candidate native profiles panic on overflow (exit 101). Generated candidate Rust uses plain `maximum + 1` inside a message argument, while baseline emits wrapping arithmetic. The probe's source SHA-256 is `47e8a7bc6a063050d4324c00befac93f00a84b1e0f442096b4b36660838fb71`; `wrapping-message-argument.json` preserves the full emitted Rust source for all six baseline/off/shadow profiles, along with exact results. The full corpus Rust diffs are in `rust-diffs.jsonl`. This is a valid-source behavior difference and blocks GO. Classification: **MIGRATION BUG**. Oracle: the Moss v0.1 practical guide says Int-min divided by -1 wraps at run time, and the baseline emits wrapping arithmetic for Int operations; the same baseline behavior is confirmed by Fast Debug and native default/`-O`. Reproducer: `tests/phase27/adversarial/cp-f-phase27-3/wrapping-message-argument.moss`. Candidate lowering must preserve wrapping for the nested message argument.

## Corpus differential

The independent streamed differential covered the 2,138-source union: all 2,118 paths from the baseline tree and 20 candidate-only paths. It included 776 example sources, 934 test sources, and 306 benchmark sources. Each row contains full check JSON for baseline, candidate `MOSS_EST=off`, and candidate `MOSS_EST=shadow`; accepted `main` sources also contain bounded Fast Debug results and default/`-O` Rust emission, compilation, and native execution results.

Verdict totals over that union: baseline 1,403 accept / 735 reject; candidate-off 1,403 / 735; candidate-shadow 1,401 / 737 plus 9 process failures (seven signals and two timeouts). The baseline and candidate-off check payloads differ on one source-free SDK fixture: `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss`, where baseline reports `RECOVERY_PATTERN_REQUIRES_VARIANT` and candidate reports `UNKNOWN_ERROR_TYPE` at line 57. The complete JSON for all three checks is retained in the raw corpus row. Both compiler versions pass the fixture's actual ABI-v9 source-free package gate with 14 providers and 5 Team A modules; the package executes and prints `advanced data source-free consumer ok`. This package-context result is recorded in `tests/phase27/adversarial/cp-f-phase27-3/logs/source-free-{baseline,candidate}.log`. The standalone fixture lacks its provider interfaces, so the package behavior is equal; the standalone diagnostic delta remains an observed diagnostic difference, not a claimed parity pass.

Candidate shadow has seven reproducible SIGABRTs. Five are in `Checker::tree_split_binary`, all at `src/semantic_checker.cpp:10939`, with `Assertion 'result == legacy' failed`:

| Original source | Exact comparison-containing fragment | Comparison operator set |
| --- | --- | --- |
| `examples/swarm/Surface/repros/swarm_067_pipe_comparison_diagnostic.moss` | `v |> count > 0` | `==`, `!=`, `<=`, `>=`, `<`, `>` |
| `examples/swarm/control_flow_2026/iterative_planner/failed_attempts/02_untyped_vector_parameters.moss` | `index < tasks |> count` | `==`, `!=`, `<=`, `>=`, `<`, `>` |
| `examples/swarm/control_flow_2026/iterative_planner/failed_attempts/03_pipeline_count_in_condition.moss` | `index < selected |> count` | `==`, `!=`, `<=`, `>=`, `<`, `>` |
| `examples/swarm/control_flow_2026/simulation_engine/failed_attempts/02_while_pipeline_condition.moss` | `index < values |> count` | `==`, `!=`, `<=`, `>=`, `<`, `>` |
| `examples/swarm/expression_surface_2026/numerical_tool/failed_attempts/08_pipeline_comparison_precedence.moss` | `energies |> filter(_ >= 100) |> sum >= 100` | `==`, `!=`, `<=`, `>=`, `<`, `>` |

Each original full source reproduces against baseline/off/shadow, with baseline and candidate-off returning matching `UNKNOWN_SYMBOL_OR_TYPE` diagnostics and candidate-shadow aborting. Reduced probes for the expression fragments are retained in `tree-split-fragments.json`; original source SHA-256 values and the exact compiler assertion stderr are in `shadow-assertions.json`. The operator set is the checker's comparison dispatch set at `tree_split_binary` callers. The assertion itself does not print the runtime string argument; no compiler source was instrumented under the no-source-edit constraint.

Two additional candidate-shadow aborts occur at `src/est/expression_query.cpp:460` in `project_simple_call`, `Assertion 'legacy == result' failed`, on `tests/phase27/adversarial/cp-ca9e8ca/nested-stage.moss` and `tests/phase27/adversarial/cp-ca9e8ca/stage-flat.moss`. The raw checks preserve exit codes, stderr, and source hashes.

Two shadow checks time out at the 120-second checker limit: `benchmarks/compiler_torture/fixtures/specialization/w2_spec_distinct_n032.moss` and `.../w3_spec_repeated_n032.moss`. These are unresolved shadow-mode timeouts, not verdict parity passes.

Fast Debug covered 941 accepted `main` sources. The 8-second initial bound produced 11 timed-out source rows. Two candidate-off mismatches were rerun with a 120-second bound and all baseline/off/shadow executions completed with equal output (`w3_spec_repeated_n016`: `1536`; `w2_spec_distinct_n016`: `5120`). Nine source rows remain timed out at the 8-second bound: `benchmarks/compiler_torture/fixtures/examples/functional_pipeline.moss`, `examples/functional_pipeline.moss`, and `examples/frontend_syntax.moss` time out in all modes; candidate-shadow only timed out on `benchmarks/compiler_torture/fixtures/specialization/w3_spec_repeated_n008.moss`, `benchmarks/compiler_torture/fixtures/traits/w8_traits_dispatch_c100.moss`, `benchmarks/compiler_torture/fixtures/inference/w5_infer_wide_w100.moss`, `benchmarks/compiler_torture/fixtures/specialization/w2_spec_distinct_n008.moss`, `examples/swarm/modules/calc_interpreter/repro/natural_full/src/main.moss`, and `examples/swarm/modules/calc_interpreter/src/main.moss`. Raw initial and follow-up outputs are under `fastdebug-timeouts-followup.json` and `differential.jsonl`. No claim of complete Fast Debug parity is made for these nine.

Native behavior ran for 941 accepted sources in default and Moss `-O` emissions. Reconciliation of raw process stderr removes only ephemeral temp filenames and thread IDs. The sole corpus output-order difference was `phase21c_concurrent_roots.moss`; this is intentionally concurrent. A follow-up supplied its four expected input files and ran each baseline/candidate profile eight times. All 48 runs produced the expected sorted values `[1260, 2001, 3003, 4065]`; schedules varied from two to five unique line orders. The source and complete repeats are retained in `concurrent-roots-followup.json`.

Rust emission differs between baseline and candidate on four source paths in both default and Moss `-O` output: `examples/checkout.moss`, `projects/cache/src/main.moss`, `projects/rolling_window/src/main.moss`, and the `benchmarks/compiler_torture/fixtures/examples/checkout.moss` mirror. Exact unified Rust diffs, hashes, and profile labels are in `rust-diffs.jsonl`. They show integer operations in nested message arguments changing from `.wrapping_*` calls to Rust's ordinary operators. The wrapping boundary reproducer confirms a behavior change, so these output differences are not adjudicated as harmless text changes.

## Sanitizer scan

The full frozen 2,138-source corpus was checked with an independently built ASan+UBSan compiler in shadow mode. Binary SHA-256: `3cdec1e3398c0e3c6fb13a13ad3b3e90fca5fc532b1faee676fda485a7843c9a`. The scan completed every source and found zero ASan/UBSan text markers and zero malformed-JSON cases. It also recorded seven SIGABRTs and two 90-second checker timeouts, matching the shadow assertion and n032 cases above. Therefore the sanitizer scan is **not clean**; all nine process failures are documented, and no sanitizer finding is claimed.

Raw per-source results: `tests/phase27/adversarial/cp-f-phase27-3/sanitizer.jsonl`. Summary: `docs/phase27/adversarial/CP_F_PHASE27_3_SANITIZER.json`.

## Reader gates

At the frozen candidate SHA, `python3 tests/tooling/check_phase27_string_debt.py --require-zero <consumer>` returned zero for checker, emitter, reach-walk, and Fast Debug. Parser and optimizer were checked separately and remain nonzero: parser 23 sites, optimizer 10. Their status is not folded into the checker/emitter gate. Per-consumer raw output is in `tests/phase27/adversarial/cp-f-phase27-3/logs/reader-gates.log` and the structured status in `docs/phase27/adversarial/CP_F_PHASE27_3_READER_GATES.json`.

## `.mossi` checks

`check_phase27_mossi_baseline.py CANDIDATE BASELINE` passed for provider and app interfaces after recomputing compiler-identity-derived fingerprint fields; normalized `.mossi` bytes and execution-body hashes match. `check_phase27_est_mossi.py CANDIDATE` passed: candidate off/shadow raw `.mossi` hashes match, one imported generic ABI-v9 body and two Section 2 imported bodies were exercised. The independent source-free ABI-v9 package gate passed on baseline and candidate with the same program output. Exact reports and logs are adjacent to this report under `tests/phase27/adversarial/cp-f-phase27-3/`.

## Commands and scope

All builds and review artifacts used isolated worktrees under `tmp/cpf-phase27-3/{baseline,candidate,evidence}`. Compiler builds used `make --jobs="$(nproc)" moss-dev`; logs are preserved under `tests/phase27/adversarial/cp-f-phase27-3/logs/`. Sanitizer build command:

```sh
make --jobs="$(nproc)" moss-dev COMPILER_DIR=build/compiler-asan \
  CXXFLAGS_DEV='-std=c++17 -Wall -Wextra -pedantic -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  LDFLAGS='-fsanitize=address,undefined' MOSS_LDFLAGS=
```

Full differential command:

```sh
CPF_JOBS=16 CPF_FAST_TIMEOUT=8 CPF_NATIVE_TIMEOUT=20 \
python3 tests/phase27/adversarial/cp-f-phase27-3/differential.py BASELINE_MOSS CANDIDATE_MOSS
```

Sanitizer command:

```sh
CPF_SAN_JOBS=8 CPF_SAN_TIMEOUT=90 \
python3 tests/phase27/adversarial/cp-f-phase27-3/sanitize.py CANDIDATE_ASAN_UBSAN_MOSS
```

No other full gate or compiler-perf run was started concurrently with this CP-F corpus/sanitizer work. No source files were edited. The complete streamed corpus, sanitizer rows, normalized summary, and exact reproducers are committed on `review/cp-f-phase27-3`.


## Overflow correction follow-up

Fixed at candidate checkpoint `NEXT COMMIT`: `expression_view_from_est` had reused a nested EST handle because its text fingerprint matched, even though its source span was relative to the parent expression. The emitter now requires a retained node span of `[0, expression.size())`; otherwise it reparses the exact child string. This preserves source ownership and restores EST binary projection. Build and the CP-F repro passed for baseline, candidate off, and candidate shadow across Fast Debug, native default, and native `-O`; every path prints `-9223372036854775808`. Focused EST and all four reader lint gates pass. Fresh CP-F re-review is still required.
