# Phase 27.5.5 report: checker-owned expression and pipeline types

Candidate: `eccd2103` (`eccd2103ae1abb67df31d2f9ca3c54349b778b69`). First parent for phase-start performance: `274f8485`; adjacent baseline: `phase27-5-4-m2` (`6e07d0b8`). The type implementation leaves the 10 frozen ownership debt entries unchanged.

## Type facts and consumer fallbacks

The emitter's string-only `generated_expr_type` entry point now throws `MOSS_INTERNAL_INVARIANT`; its `EmitterExpr` overload reads the checked type or fails closed (`src/native_generator.cpp:3138–3265`). Fast Debug's checked expression and pipeline type paths likewise fail closed, rather than deriving a Moss type from runtime values (`src/interpreter.hpp:1203–1317`). The functional optimizer uses the checked pipeline stage type and declines/fails when it is absent (`src/functional_optimizer.cpp:640–688`). The final 1,828-source sweep below measures the emitter and optimizer's every type query in default and `-O` emission.

| Consumer | Checked type answers | Missing answers | Type inference | Expression reparses | Text node lookups |
| --- | ---: | ---: | ---: | ---: | ---: |
| Emitter (default and `-O` combined) | 174,024 | 0 | 0 | 0 | 0 |
| Optimizer | 1 | 0 | 0 | 0 | 0 |
| Fast Debug | Type queries are guarded by checked-fact access and perturb/drop tests; see test matrix below. | 0 successful runs after dropped type facts | 0 | 0 | 0 |

Workload: 1,828 tracked `.moss` files from tests and examples, 337 package manifests inventoried, 3,656 default/`-O` source-emission commands. The phase27-5-3-c1 parent had 1,406 failed emissions (703 each mode); candidate has 1,410 (705 each mode). The two additional source cases, each in both modes, are `examples/swarm/expression_surface_2026/validation_engine/failed_attempts/17_formatter_return_grouping_native.moss` and `tests/phase27/adversarial/cp-e-blind-914d59d/probe-sources/cpe-probe-incomplete_index.moss`. Both pass `moss check`; parent emits Rust that is already known invalid for these probes, while candidate fails closed on missing checked call-target / expression facts. Neither is a valid generated-program behavior regression, but this is a measured emission-status difference and is not hidden. Candidate counters are all zero for reparses, functional-pipeline reparses, consumer expression parses, text-node lookups, and consumer type inference. The coverage counter only counts consumer type requests; it does not claim package-context emission, as the package manifests are inventoried but not separately run here. Raw result: `tmp/phase2754-emitter-counts/1793619016465805546/summary.json`.

The fixed Phase 27.5.4 ratchet workload reports checker-internal expression
reparses=55,160 and text-cache lookups=16,456,102 (previous recorded values
54,037 and 16,261,580). Under the explicit 27.5.4 re-scope these checker
counts are report-only; the runtime ratchet continues to gate driver rewrites
and all consumers, with Fast Debug, emitter and optimizer at zero.

No type entries remain in `tests/tooling/fixtures/phase27_5_4_fact_hook_debt.json`; its 10 ownership entries are unchanged. Built-in expression type and pipeline type perturb/drop fixtures pass through Fast Debug, native and `-O`. Agent 2 must select three additional unseen, accepted sources for each class and run them through the same registered harness before approval; record exact paths and results here.

## Lowering-only Rust types

These are Rust ABI/runtime forms, not alternative Moss type answers:

- `&T` and `&mut T`: encode the checker's READ/WRITE parameter effects as Rust borrowing (`src/handler_lowering.inc:96–107`, `src/native_generator.cpp:2399–2501`).
- `MossRead<'a,T>`, `MossWrite<'a,T>`, and `MossAbsent<T>`: capability wrappers for domain shared-state message ABI and absent effect slots (`src/handler_lowering.inc:96–107`, `src/handler_runtime.cpp:30–41`).
- `Result<T,__MossRaised>` and `__MossRaised` (including generated provider carrier enums): encode the Moss effect/raise transport across Rust function and handler boundaries (`src/native_generator.cpp:677–725, 8717–8727, 9076–9086`).
- `std::sync::RwLock<T>` and its read/write guards: implement the checker-derived domain synchronization plan and runtime lock acquisition (`src/handler_lowering.inc:865, 994`, `src/handler_runtime.cpp:19–24`).
- `Arc<...>` and generated `*Ref`/runtime state wrappers: provide thread-safe domain runtime identity and storage; Moss domain handles themselves are static capabilities rather than first-class value types (`src/native_generator.cpp:9043–9044`).
- `std::marker::PhantomData<T>` in `MossAbsent<T>`: Rust representation marker for an ABI slot with no Moss runtime value (`src/handler_lowering.inc:96–99`).

`MossStr`, `MossSet`, `MossMap`, `Option<T>`, and `Vec<T>` are not listed as lowering-only: they are runtime representations of actual Moss value types. Agent 2 reviews this list; all placeholder citations must be resolved before merge.

## Paired performance

Both reports use the same 77 fixtures and candidate code `ff17ecf6`, 3 interleaved timing trials and one separate profiling trial. The phase-start baseline is `274f8485`; the adjacent baseline is `phase27-5-4-m2`. Aggregate is the sum of per-fixture wall-time medians.

| Comparison | Baseline aggregate | Candidate aggregate | Change |
| --- | ---: | ---: | ---: |
| vs phase27-5-4-m2 | 5.789 s | 5.976 s | +3.2% |
| vs phase-start `274f8485` | 15.029 s | 5.967 s | -60.3% |

| Workload | phase-start baseline | Candidate | Change | m2 baseline | Candidate | Change |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| W2 `w2_spec_distinct_n032` | 1899.95 ms | 2157.02 ms | +13.5% | 2128.57 ms | 2153.57 ms | +1.2% |
| W4 `w4_infer_deep_d018` | 7426.97 ms | 32.01 ms | -99.6% | 32.70 ms | 33.19 ms | +1.5% |
| W5 `w5_infer_wide_w100` | 126.19 ms | 150.83 ms | +19.5% | 138.47 ms | 146.74 ms | +6.0% |
| W8 `w8_traits_dispatch_c100` | 78.23 ms | 87.55 ms | +11.9% | 88.86 ms | 95.26 ms | +7.2% |

| W2 n032 peak memory build | Peak RSS |
| --- | ---: |
| phase-start `274f8485` | 34,748 KiB |
| m2 baseline | 63,756 KiB |
| candidate, normal optimized flags (checker TU `-Os`) | 64,624 KiB |
| candidate, plain `-O2` for every translation unit | 65,404 KiB |

The plain-`-O2` binary was rebuilt from `eccd2103` in disposable `tmp/phase2755-o2`; its W2 peak is 780 KiB (1.2%) above the normal candidate. Each figure is the maximum from three direct repetitions of the identical fixture. No compiler flags in tracked files were changed. In the current m2 paired run, `rw_projects_rolling_window` measured 52.80 ms vs 58.70 ms (+11.2%). A nine-trial interleaved repeat measured m2 median 54.84 ms (52.13–62.61) and candidate median 60.73 ms (55.31–71.41); the ranges overlap, so the focused result is within the established noise rule. The current phase-start paired run measured 92.20 ms vs 57.64 ms (-37.5%).

### The 23 real-world workloads

Each cell gives baseline median and candidate median with percentage change. Columns show the two paired baselines independently.

| Workload | m2 baseline | Candidate vs m2 | phase-start baseline | Candidate vs phase-start |
| --- | ---: | ---: | ---: | ---: |
| `rw_example_checkout` | 3.83 | 3.86 ms (+0.8%) | 3.67 | 3.92 ms (+6.8%) |
| `rw_example_collections` | 3.34 | 3.47 ms (+3.8%) | 3.21 | 3.63 ms (+12.9%) |
| `rw_example_counter` | 1.90 | 1.94 ms (+2.2%) | 1.69 | 1.88 ms (+10.9%) |
| `rw_example_domains` | 2.05 | 2.62 ms (+28.2%) | 1.88 | 2.02 ms (+7.0%) |
| `rw_example_pipeline` | 1.77 | 2.25 ms (+27.3%) | 1.51 | 1.70 ms (+12.8%) |
| `rw_packages_regex` | 705.42 | 727.89 ms (+3.2%) | 1467.47 | 738.88 ms (-49.6%) |
| `rw_phase106d1_borrowed_reads` | 12.91 | 13.31 ms (+3.1%) | 12.29 | 13.39 ms (+8.9%) |
| `rw_phase106d1_read_projection` | 9.65 | 10.12 ms (+4.8%) | 9.76 | 11.22 ms (+15.0%) |
| `rw_phase106d_handler_2pl` | 7.56 | 8.17 ms (+8.2%) | 7.50 | 7.66 ms (+2.1%) |
| `rw_phase15_15_enum_positive` | 5.89 | 6.20 ms (+5.3%) | 5.81 | 6.02 ms (+3.6%) |
| `rw_phase20_fileio_helper` | 2.52 | 2.48 ms (-1.6%) | 2.51 | 2.38 ms (-5.3%) |
| `rw_phase21_fileio_root_cleanup` | 3.18 | 3.15 ms (-0.8%) | 3.75 | 3.01 ms (-19.7%) |
| `rw_phase24_string_concat` | 6.26 | 7.82 ms (+25.0%) | 5.57 | 8.95 ms (+60.7%) |
| `rw_phase25_integer_overflow` | 6.19 | 7.35 ms (+18.8%) | 5.68 | 6.46 ms (+13.7%) |
| `rw_phase46_semantic_barriers` | 7.53 | 8.32 ms (+10.5%) | 6.38 | 7.47 ms (+17.1%) |
| `rw_phase47_iteration` | 8.73 | 8.89 ms (+1.9%) | 8.16 | 8.82 ms (+8.1%) |
| `rw_phase4_functional` | 15.62 | 14.65 ms (-6.2%) | 14.10 | 14.23 ms (+1.0%) |
| `rw_projects_cache` | 31.32 | 35.91 ms (+14.7%) | 55.47 | 39.67 ms (-28.5%) |
| `rw_projects_rolling_window` | 52.80 | 58.70 ms (+11.2%) | 92.20 | 57.64 ms (-37.5%) |
| `rw_sdk_datastructures` | 263.83 | 267.07 ms (+1.2%) | 564.60 | 271.46 ms (-51.9%) |
| `rw_swarm_047_reduce_map` | 8.00 | 9.75 ms (+21.9%) | 8.37 | 8.30 ms (-0.8%) |
| `rw_swarm_066_for_range` | 9.19 | 10.49 ms (+14.2%) | 8.54 | 8.93 ms (+4.7%) |
| `rw_tests_branch_confined_open` | 3.27 | 3.34 ms (+2.1%) | 2.98 | 3.29 ms (+10.5%) |

## Gates and known suite failures

Final default and shadow suites, three-engine parity, no-regression parity, reviewer-selected unseen fixtures, and Agent 2 outcome are pending. Previously documented C1 failures were 3/165: conformance gate 10 (`compiled_only_custom_key`) and the two `swarm_035_nested_mutating_calls.moss` Rust E0499 jobs; final suite results must show no additions. Current paired raw performance data: `tmp/compiler-perf/20261102-033425/LOCAL_REPORT.md` and `tmp/compiler-perf/20261102-033618/LOCAL_REPORT.md`.
