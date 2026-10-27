# Phase 26.1 C1 Results — Function-Scoped Check & Package Rules

- Commit SHA: `5cdf6528d15bbb1e3aa1fc72c4b76a3bb1463dea`
- Branch: `phase26-1-m1`

## Scope and Invariants
- **R3**: `moss check <file>` checks everything transitively reachable from that file, function by function within its package. Sibling method calls, implicit iterators, module-qualified functions, and nominal field/domain type dependencies are fully retained. Unreachable helper and sibling declaration errors are omitted from file check scope.
- **R4**: Package-wide semantic rules (duplicate declarations, coherence, route conflicts, cross-handler analyses) run only under whole package modes (`margo build --check`, `margo build`).
- **Diagnostic Oracle**: **Open.** The earlier report overstated this gate. There is a partial-reach identity fixture, but no corpus-wide oracle yet. Architect review of `ad3276ca` was NO-GO pending a target-aware oracle and exact diagnostic evidence.
- **Partial-reach fixture**: Verifies file check visits exactly reachable functions by identity (`m1_partial.moss:main`, `reached_top`, `reached_leaf`), whereas package mode checks all 6 functions.

## Validation Gates
1. `make --jobs="$(nproc)" check`: 145/145 passed after the current source/test changes; suite wall time 234.68 s. Fast Debug parity passed as part of the suite.
2. `MOSS_M1_ENFORCE_TARGETS=1 make package-perf`: all 70 scenario rows met; latest output is under `tmp/compiler-perf/m1-20261026-223043/`.
4. Full Corpus Differential (2,064 sources):
   - The original C1 run recorded 12 differences versus the pre-C1 branch; these are still under review and are listed below.
   - A closeout differential against frozen C1 checkpoint `ad3276ca` checked all 2,064 sources with generated-Rust hashes: 1,336 accepted on both sides, byte-identical verdict/hash rows, zero additional differences.
   - No hangs / timeouts in either run. The `ad3276ca` comparison proves closeout changes add no corpus differences; it does not classify the original 12.
5. Package test-target check: `margo build --check` now checks the complete Tests target (`src + tests`) semantically (without executing tests or emitting Rust) after checking the package `src` target. Test-source contents are included in the check fingerprint. Focused probes cover a bad sibling test body and duplicate declarations across separate test files. The C1 corpus-case gate and direct calc-interpreter check confirm the package rejects on the unchanged sibling diagnostic `TEST_ASSERTION_CONFIGURATION_ERROR` at `tests/parser_test.moss:15`.
6. The 12 corpus-case gate prints a corresponding Margo rejection for each changed file, using two package target checks (Calc tests and Toolkit app). This confirms package rejection and code/source/line; it does not enumerate all package diagnostics.
7. Snapshot workflow: updated the agent workflow skill and API guide to state that `moss check <file>` does not persist the native-lowering snapshot. Run `moss build` before relying on incremental impact results; absent snapshots fall back conservatively.

## The 12 Corpus Differences

| Target file group | Previous full-target diagnostic | Current per-file result | Margo package check |
|---|---|---|---|
| `calc_interpreter/.../tests/{calc,env,eval,lexer,report,token}_test.moss` (6 files) | `TEST_ASSERTION_CONFIGURATION_ERROR:15` | Accepted; no generated Rust | `margo build --check` now rejects at sibling `tests/parser_test.moss:15` with the same code/message. |
| `text_toolkit/.../{src/inventory.moss,src/main.moss,src/table.moss,tests/inventory_test.moss,tests/stats_test.moss,tests/table_test.moss}` (6 files) | `UNSUPPORTED_STRING_ORDERING:106` | Five accepted without Rust; `src/main.moss` reports `UNSUPPORTED_STRING_ORDERING:127` | Package check rejects `UNSUPPORTED_STRING_ORDERING` at `src/strings.moss:106`; the package test target also rejects the same code. |

The gate prints the package rejection beside each of the 12 changed rows. Calc rows map to the same `TEST_ASSERTION_CONFIGURATION_ERROR` at `tests/parser_test.moss:15`; Toolkit rows map to `UNSUPPORTED_STRING_ORDERING` at `src/strings.moss:106`. It does not establish the required corpus-wide reachable-set oracle; the toolkit file and package diagnostics point at different occurrences, and package checking currently returns its first diagnostic only.

## Local Compiler Performance

`MOSS_PERF_REPS=5 MOSS_PERF_BASELINE=origin/main make compiler-perf` completed on `DESKTOP-0T772KP` (WSL2, 16 logical CPUs) against `origin/main` at `d109b8f17276f70b591432fd345d57e727f17501`. The run used 57 fixtures, five interleaved timing repetitions, and one separate profiling sample. The sum of per-fixture medians changed from 5.337 s to 5.272 s (-1.2%). No fixture exceeded the suite's slowdown noise floor. This was not a certified run because the worktree was dirty. Report and counters are under `tmp/compiler-perf/20261026-222830/`. The eight attribution workload medians were:

| Fixture | Baseline | Candidate | Change |
|---|---:|---:|---:|
| `example_checkout` | 3.69 ms | 3.64 ms | -1.2% |
| `rw_packages_regex` | 752.71 ms | 680.21 ms | -9.6% |
| `rw_projects_rolling_window` | 50.94 ms | 47.24 ms | -7.2% |
| `rw_sdk_datastructures` | 287.06 ms | 270.85 ms | -5.6% |
| `w2_spec_distinct_n032` | 1855.92 ms | 1878.40 ms | +1.2% |
| `w3_spec_repeated_n032` | 912.80 ms | 928.03 ms | +1.7% |
| `w4_infer_deep_d018` | 25.56 ms | 28.65 ms | +12.1% |
| `w5_infer_wide_w100` | 128.16 ms | 125.39 ms | -2.2% |

The run was not certified: the C1 working tree was dirty, and certification requires compiler sources identical to `origin/main`. Raw report and counters were produced under `tmp/compiler-perf/20261026-215528/`.

## Architect Review

Architect verdict: **NO-GO** for C1 closeout. The 12-row Margo mapping, complete semantic Tests target, and scope-identity cases are now covered. The literal corpus-wide diagnostic-set oracle remains absent because the checker and package command expose only the first error. Architect says no comparison of one result per file/package can prove both set inclusions; an oracle-only diagnostic enumeration path or an explicit acceptance narrowing is required. Do not start T1 until this is resolved and the final C1 tree receives GO.
