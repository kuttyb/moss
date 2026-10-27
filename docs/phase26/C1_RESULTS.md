# Phase 26.1 C1 Results — Function-Scoped Check & Package Rules

- Implementation SHA: `4b14c07a35e44ab3977f6f9eb2ef62e9eb9241f0`
- Branch: `phase26-1-m1`

## Scope and Invariants
- **R3**: `moss check <file>` checks everything transitively reachable from that file, function by function within its package. Sibling method calls, implicit iterators, module-qualified functions, and nominal field/domain type dependencies are fully retained. Unreachable helper and sibling declaration errors are omitted from file check scope.
- **R4**: Package-wide semantic rules (duplicate declarations, coherence, route conflicts, cross-handler analyses) run only under whole package modes (`margo build --check`, `margo build`).
- **C1 closeout gates**: Fail-fast G-a/G-b are implemented in `tests/tooling/check_phase26_1_c1_corpus_scope_gates.py`; latest run passes with zero failures (counts below). Earlier multi-error oracle experiments are not current gates.
- **Partial-reach fixture**: Verifies file check visits exactly reachable functions by identity (`m1_partial.moss:main`, `reached_top`, `reached_leaf`), whereas package mode checks all 6 functions.

## Validation Gates
1. `make --jobs="$(nproc)" check`: **145/145 passed**, 0 skipped, suite wall time 234.27 s. Fast Debug parity passed as part of the suite.
2. `MOSS_M1_ENFORCE_TARGETS=1 make package-perf`: **70 scenario rows passed** on `4b14c07a`; generated report `tmp/compiler-perf/m1-20261027-073227/M1_BASELINE.md`.
3. `make c1-corpus-gates`: **passed with zero failures** on `4b14c07a`. It checked 2,064 tracked sources, 421 package roots, 335 accepted and 86 rejected packages, and 2,032 target files. G-a accepted-package sources: **633/633**. G-b reachable first package diagnostics: **68/68** rejected with the same code. There were 17 rejected packages with no file trace reaching their first error, nine missing/stale interface terminal packages, and 12 individually main-reproduced baseline package errors. Output: `tmp/phase26-1-m1/c1-one-way-gates-final.out`.
4. Source-root allowlist regressions passed in `check_phase26_1_f1_interface_rules.py`; the 12-row C1 package case gate passed with all six Calc and six Toolkit rows mapped to the expected package diagnostic. The geometry mismatch and malformed Margo dependency path are corrected.
5. Full Corpus Differential against `origin/main` (`d109b8f17276f70b591432fd345d57e727f17501`), 2,064 sources including diagnostic codes and generated-Rust hashes: **FAIL / 14 differences**. The six Calc and six Toolkit rows below are the documented 12 package-scope omissions. Two additional sources now reject with `MODULE_IDENTITY_UNRESOLVED` although `origin/main` accepts them: `examples/priority_queue/src/main.moss` (`python-priority-queue`) and `examples/swarm/Julia/CircularBuffer/src/main.moss` (`julia-circular-buffer`). This was observed on the unchanged C1 code commit `4b14c07a`; no additional C1 compiler edits were made. The extra rows are unresolved and prevent claiming a green full corpus differential. Results: `tmp/phase26-1-m1/corpus-origin-main.tsv` and `tmp/phase26-1-m1/corpus-candidate-origin-main.tsv`.
6. Package test-target check: `margo build --check` checks the complete Tests target (`src + tests`) semantically without executing tests or emitting Rust. Test-source contents are included in the check fingerprint. Focused probes cover a bad sibling test body and duplicate declarations across separate test files.
7. Snapshot workflow: updated the agent workflow skill and API guide to state that `moss check <file>` does not persist the native-lowering snapshot. Run `moss build` before relying on incremental impact results; absent snapshots fall back conservatively.

## G-b No-Hit Package First-Error Audit

The earlier corpus gate reported 17 rejected consumer package roots whose own
tracked files did not trace the package graph's first diagnostic. The table
records every first-error code and its owning declaration based on the
candidate audit. A dependency-owned diagnostic must be checked in the owning
package; file mode must not read that dependency's Moss sources.

| Rejected package root | First code | Error owner / audit |
|---|---|---|
| `examples/swarm/modules/algo_chain/repro/generic_poisons_import/split/consumer` | `MOSS_COMPILE_ERROR` | Imported `provider.util.fill`; unused by consumer `main`. Dependency body is re-evaluated in the consumer package check; T1 scope, not a C1 reach defect. |
| `examples/swarm/modules/algo_chain/repro/natural_chain/routekit` | `INVALID_BARE_IDENTIFIER_STATEMENT` | Dependency `heapkit.MinHeap.push`, `heap.moss:28`; owner-package gate reaches it. |
| `examples/swarm/modules/algo_chain/routekit` | `INVALID_BARE_IDENTIFIER_STATEMENT` | Dependency `heapkit.MinHeap.push`, `heap.moss:28`; owner-package gate reaches it. |
| `examples/swarm/modules/lib_and_app/repro/natural_app_attempt` | `TYPE_MISMATCH` | `geolib.shapes.Triangle.area`, `shapes.moss:46`; geolib's own `margo build --check` rejects with `TYPE_MISMATCH` at `shapes.moss:48` (`method:shapes__Triangle.area@46`). Dependency body is re-evaluated in the consumer package check; T1 scope, not a C1 reach defect. |
| `examples/swarm/modules/text_toolkit/app` | `UNSUPPORTED_STRING_ORDERING` | Dependency `toolkit.strings.min_text`; this app does not call it. |
| `examples/swarm/modules/text_toolkit/probe_debug` | `UNSUPPORTED_STRING_ORDERING` | Dependency `toolkit.strings.min_text`; this probe does not call it. |
| `packages/numerics_integration` | `TYPE_MISMATCH` | Dependency test `packages/linalg/tests/linalg_test.moss`, `malformed direct matrix construction validation`; the linalg owner package's file gate reaches it. |
| `packages/sparse` | `TYPE_MISMATCH` | Same `packages/linalg` test diagnostic; dependency-owned. |
| `sdk/advanced-data-acceptance` | `TYPE_MISMATCH` | Same `packages/linalg` test diagnostic; dependency-owned. |
| `sdk/ndarray` | `TYPE_MISMATCH` | Same `packages/linalg` test diagnostic; dependency-owned. |
| `sdk/ndarray/examples/batched_normalization` | `TYPE_MISMATCH` | Same `packages/linalg` test diagnostic; dependency-owned. |
| `sdk/ndarray/examples/tensor_transforms` | `TYPE_MISMATCH` | Same `packages/linalg` test diagnostic; dependency-owned. |
| `tests/phase25/key_protocol/04-two-consumers-shared-map/alpha` | `RECORD_MAP_KEY_UNSUPPORTED` | Dependency `registry.Registry` state declaration. |
| `tests/phase25/key_protocol/04-two-consumers-shared-map/app` | `RECORD_MAP_KEY_UNSUPPORTED` | Dependency `registry.Registry` state declaration. |
| `tests/phase25/key_protocol/04-two-consumers-shared-map/beta` | `RECORD_MAP_KEY_UNSUPPORTED` | Dependency `registry.Registry` state declaration. |
| `tests/phase25/key_protocol/05-coherence-mismatch-free-proofs/app` | `RECORD_MAP_KEY_UNSUPPORTED` | Dependency `alpha.record`; invalid provider cannot emit a current interface. |
| `tests/phase25/key_protocol/05-coherence-mismatch-method-override/app` | `RECORD_MAP_KEY_UNSUPPORTED` | Dependency `alpha.record`; invalid provider cannot emit a current interface. |

`Triangle.area` and imported `util.fill` are dependency bodies re-evaluated in
the consumer's package check; both are T1 scope, not C1 reach defects. The
other rows are dependency-owned diagnostics or dependency test targets. The
G-b gate remains the file-scope oracle for the package that owns each body.

## Known Regression: Cross-Package Emacs Xref

Cross-package xref currently points to the dependency's `.mossi` file at line
0. The source-location expectation is marked as an expected failure in the
Emacs suite. Phase 26.1.1 will add declaration locations in a sidecar file,
outside the interface digest. Do not count this as a C1 gate failure or claim
the source-location behavior already works.

## The 12 Corpus Differences

| Target file group | Previous full-target diagnostic | Current per-file result | Margo package check |
|---|---|---|---|
| `calc_interpreter/.../tests/{calc,env,eval,lexer,report,token}_test.moss` (6 files) | `TEST_ASSERTION_CONFIGURATION_ERROR:15` | Accepted; no generated Rust | `margo build --check` now rejects at sibling `tests/parser_test.moss:15` with the same code/message. |
| `text_toolkit/.../{src/inventory.moss,src/main.moss,src/table.moss,tests/inventory_test.moss,tests/stats_test.moss,tests/table_test.moss}` (6 files) | `UNSUPPORTED_STRING_ORDERING:106` | Five accepted without Rust; `src/main.moss` reports `UNSUPPORTED_STRING_ORDERING:127` | Package check rejects `UNSUPPORTED_STRING_ORDERING` at `src/strings.moss:106`; the package test target also rejects the same code. |

The 12-row gate prints the package rejection beside each changed row. Calc rows map to `TEST_ASSERTION_CONFIGURATION_ERROR` at `tests/parser_test.moss:15`; Toolkit rows map to `UNSUPPORTED_STRING_ORDERING` at `src/strings.moss:106`. G-a/G-b separately check the two fail-fast implications across the tracked corpus; neither gate attempts to enumerate multiple diagnostics from one checker run.

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

## Historical Multi-Error Oracle Experiment — 2026-10-26

This experiment temporarily added multi-error checker behavior to collect a
diagnostic-set oracle. That scaffolding was removed during C1 closeout because
the checker contract remains fail-fast; multi-error reporting is out of scope.

Final local attempt result: **FAIL / uncomparable**, not a gate pass. The
tracked corpus contains 2,064 `.moss` sources. The gate ran 519 package target
queries and completed 2,029 paired or standalone file checks, reporting 675
enumerated package/terminal diagnostics; 35 source rows were not verified.
There are 28 uncomparable rows. Categories:

- 16 checker-internal failures during package/file checking (including
  unresolved callable argument, concrete method effect target, state layout,
  and message receiver), plus four signal-11 terminations. Some internal
  failures reproduce on the prior baseline; they still prevent diagnostic-set
  proof, and the adversarial review requires reduction before attributing them
  to baseline-only behavior.
- Six package/file terminal mismatches where missing/stale dependency
  interfaces differ by source or module (`routekit`, `natural_system`, and
  Toolkit app). These are not accepted as equivalent terminal results.
- One Margo fixture graph references a package without a manifest, and one
  file diagnostic (`MODULE_IDENTITY_UNRESOLVED` in `geometry_modules`) is
  absent from package output.

The earlier 12-row gate continues to pass, as do the targeted full-set oracle
comparisons for Toolkit (9 files, 6 package diagnostics) and Calc (18 files, 4
package diagnostics). Those do not supersede the corpus result. Exact terminal
matches are limited pre-body coverage, not complete package/file diagnostic
set equivalence. The Architect notes that the current acceptance rule allows a
package terminal if any one file matches while other files return an empty
complete body set. This must not be described as full-target coverage.

The historical log is under `tmp/phase26-1-m1/c1-oracle-final.out`; it is
evidence only and is not a current gate. The current fail-fast G-a/G-b gate is
the source of truth for C1 closeout.

## Known Baseline Issues

The following internal errors and crashes were reproduced on clean
`origin/main` at `d109b8f`. They predate C1 and are not C1 blockers. Commands use
the repository-local tools; package cases run `margo build` from the listed
package directory, while standalone cases run `moss check <file> --json` from
the repository root. Main predates `margo build --check`, so `margo build` is
the equivalent baseline reproduction.

| Repro path | Baseline result |
|---|---|
| `examples/swarm/Surface/Shapes/src/main.moss` | `MOSS_INTERNAL_OR_IO_ERROR: unresolved callable argument` (standalone check and package build) |
| `examples/swarm/modules/algo_chain/repro/trait_export/single/src/main.moss` | `MODULE_IDENTITY_UNRESOLVED: module 'trait_export' has no package identity` (package build) |
| `examples/swarm/modules/algo_chain/repro/trait_export/split/src/main.moss` | `MOSS_INTERNAL_OR_IO_ERROR: unresolved state layout` |
| `examples/swarm/modules/data_pipeline/repro/trait_across/split/src/main.moss` | `MOSS_INTERNAL_OR_IO_ERROR: unresolved state layout` |
| `examples/swarm/modules/domain_services/repro/natural_system/{app,common,services}` | `MOSS_INTERNAL_OR_IO_ERROR: unresolved state layout` (app and services package builds; common direct and package checks) |
| `examples/swarm/modules/domain_services/repro/trait_param/split/src/main.moss` | `MOSS_INTERNAL_OR_IO_ERROR: unresolved state layout` |
| `examples/swarm/modules/lib_and_app/repro/trait_param_export/split/src/main.moss` | `MOSS_INTERNAL_OR_IO_ERROR: unresolved state layout` |
| `examples/swarm/modules/domain_services/repro/generic_pipeline/{single,split}/src/main.moss` | SIGSEGV (standalone check; package build also fails without compiler JSON) |
| `examples/swarm/modules/lib_and_app/repro/generic_pipeline_method_placeholder/single/src/main.moss` | SIGSEGV (standalone check; package build also fails without compiler JSON) |
| `examples/swarm/expression_surface_2026/validation_engine/failed_attempts/04_pipeline_collection_inference.moss` | SIGSEGV (standalone check) |

Several errors reported by the abandoned oracle are not baseline compiler
failures: with Margo's resolved dependency environment, both `log_analytics`
file checks and its package build pass on main; its callback reachability is
fixed in C1. `heapkit`, handler-direct-call, file-I/O, source-free range, and
self-send examples return ordinary diagnostics or pass package build on main.

After these oracle and report edits, `make --jobs="$(nproc)" check` passed
145/145 locally in 236.74 s, including `shard_fast_debug`. This validates the
ordinary regression suite on the implementation tree; it does not make the
failed corpus oracle green. The Architect reviewed base `a60f21db` plus this
worktree diff and returned **NO-GO** for C1, confirming all 28 rows and the 35
unverified sources noted above.
