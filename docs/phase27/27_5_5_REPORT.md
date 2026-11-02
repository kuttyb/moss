# Phase 27.5.5 report: checker-owned expression and pipeline types

Candidate: `ff17ecf6` code, plus docs/checkpoint commit `16bc38a5`. First parent for phase-start performance: `274f8485`; adjacent baseline: `phase27-5-4-m2` (`6e07d0b8`). The type implementation leaves the 10 frozen ownership debt entries unchanged.

## Type facts and consumer fallbacks

The emitter's string-only `generated_expr_type` entry point now throws `MOSS_INTERNAL_INVARIANT`; its `EmitterExpr` overload reads the checked type or fails closed (`src/native_generator.cpp:3138–3265`). Fast Debug's checked expression and pipeline type paths likewise fail closed, rather than deriving a Moss type from runtime values (`src/interpreter.hpp:1203–1317`). The functional optimizer uses the checked pipeline stage type and declines/fails when it is absent (`src/functional_optimizer.cpp:640–688`). The final 1,828-source sweep below measures the emitter and optimizer's every type query in default and `-O` emission.

| Consumer | Checked type answers | Missing answers | Type inference | Expression reparses | Text node lookups |
| --- | ---: | ---: | ---: | ---: | ---: |
| Emitter (default and `-O` combined) | 271,538 | 0 | 0 | 0 | 0 |
| Optimizer | 1 | 0 | 0 | 0 | 0 |
| Fast Debug | Type queries are guarded by checked-fact access and perturb/drop tests; see test matrix below. | 0 successful runs after dropped type facts | 0 | 0 | 0 |

Workload: 1,828 tracked `.moss` files from tests and examples, 337 package manifests inventoried, 3,656 default/`-O` source-emission commands. Both Phase 27.5.3 baseline and candidate have 1,410 failed emissions (705 each mode); no status-count regression. Candidate counters are all zero for reparses, functional-pipeline reparses, consumer expression parses, text-node lookups, and consumer type inference. The coverage counter only counts consumer type requests; it does not claim package-context emission, as the package manifests are inventoried but not separately run here. Raw result: `tmp/phase2754-emitter-counts/1793613540050453503/summary.json`.

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
| vs phase27-5-4-m2 | 5.925 s | 6.092 s | +2.8% |
| vs phase-start `274f8485` | 15.000 s | 6.100 s | -59.3% |

| Workload | phase-start baseline | Candidate | Change | m2 baseline | Candidate | Change |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| W2 `w2_spec_distinct_n032` | 1946.85 ms | 2193.24 ms | +12.7% | 2170.81 ms | 2212.82 ms | +1.9% |
| W4 `w4_infer_deep_d018` | 7302.76 ms | 33.14 ms | -99.5% | 35.13 ms | 34.21 ms | -2.6% |
| W5 `w5_infer_wide_w100` | 128.28 ms | 148.02 ms | +15.4% | 149.53 ms | 144.34 ms | -3.5% |
| W8 `w8_traits_dispatch_c100` | 77.70 ms | 89.91 ms | +15.7% | 85.95 ms | 87.84 ms | +2.2% |

| W2 n032 peak memory build | Peak RSS |
| --- | ---: |
| phase-start `274f8485` | 34,760 KiB |
| m2 baseline | 63,828 KiB |
| candidate, normal optimized flags (checker TU `-Os`) | 64,800 KiB |
| candidate, plain `-O2` for every translation unit | 65,664 KiB |

The plain-`-O2` binary was built in disposable `tmp/phase2755-o2` from the same candidate source. The observed peak is 864 KiB (1.3%) above the normal build. No compiler flags in tracked files were changed. The paired m2 report identified `rw_projects_cache` as a possible small-fixture slowdown (+3.11 ms, +9.3%); a nine-trial interleaved repeat overlapped in range (m2 31.67–38.00 ms, candidate 33.07–41.39 ms), so this is within the established noise rule.

### The 23 real-world workloads

Each cell gives baseline median and candidate median with percentage change. Columns show the two paired baselines independently.

| Workload | m2 baseline | Candidate vs m2 | phase-start baseline | Candidate vs phase-start |
| --- | ---: | ---: | ---: | ---: |
| `rw_example_checkout` | 3.71 | 3.94 ms (+6.3%) | 3.70 | 3.85 ms (+4.1%) |
| `rw_example_collections` | 3.91 | 3.60 ms (-8.0%) | 4.22 | 3.52 ms (-16.7%) |
| `rw_example_counter` | 1.85 | 1.93 ms (+4.0%) | 1.82 | 1.87 ms (+2.8%) |
| `rw_example_domains` | 1.97 | 1.98 ms (+0.7%) | 2.01 | 2.05 ms (+2.3%) |
| `rw_example_pipeline` | 1.70 | 1.74 ms (+2.2%) | 1.65 | 1.70 ms (+3.3%) |
| `rw_packages_regex` | 715.37 | 740.93 ms (+3.6%) | 1487.05 | 746.85 ms (-49.8%) |
| `rw_phase106d1_borrowed_reads` | 14.15 | 13.44 ms (-5.0%) | 12.57 | 13.23 ms (+5.2%) |
| `rw_phase106d1_read_projection` | 10.15 | 9.90 ms (-2.5%) | 9.82 | 11.06 ms (+12.6%) |
| `rw_phase106d_handler_2pl` | 7.25 | 8.01 ms (+10.4%) | 7.30 | 7.78 ms (+6.6%) |
| `rw_phase15_15_enum_positive` | 6.26 | 7.57 ms (+21.0%) | 5.56 | 6.01 ms (+8.0%) |
| `rw_phase20_fileio_helper` | 2.38 | 2.79 ms (+17.6%) | 2.35 | 2.61 ms (+11.2%) |
| `rw_phase21_fileio_root_cleanup` | 2.96 | 2.92 ms (-1.3%) | 2.81 | 3.09 ms (+10.2%) |
| `rw_phase24_string_concat` | 6.68 | 6.54 ms (-2.1%) | 6.38 | 6.65 ms (+4.2%) |
| `rw_phase25_integer_overflow` | 8.28 | 6.29 ms (-24.0%) | 6.67 | 8.15 ms (+22.2%) |
| `rw_phase46_semantic_barriers` | 7.98 | 7.66 ms (-4.1%) | 6.33 | 7.79 ms (+23.0%) |
| `rw_phase47_iteration` | 8.74 | 8.35 ms (-4.5%) | 7.96 | 8.75 ms (+9.9%) |
| `rw_phase4_functional` | 15.39 | 15.70 ms (+2.0%) | 11.81 | 16.05 ms (+35.9%) |
| `rw_projects_cache` | 33.28 | 36.39 ms (+9.3%) | 55.58 | 37.62 ms (-32.3%) |
| `rw_projects_rolling_window` | 55.71 | 58.65 ms (+5.3%) | 96.54 | 61.66 ms (-36.1%) |
| `rw_sdk_datastructures` | 274.34 | 281.04 ms (+2.4%) | 565.05 | 270.28 ms (-52.2%) |
| `rw_swarm_047_reduce_map` | 8.13 | 9.55 ms (+17.5%) | 10.21 | 9.01 ms (-11.7%) |
| `rw_swarm_066_for_range` | 8.81 | 8.95 ms (+1.6%) | 9.88 | 8.86 ms (-10.4%) |
| `rw_tests_branch_confined_open` | 3.16 | 3.34 ms (+5.6%) | 2.86 | 3.36 ms (+17.2%) |

## Gates and known suite failures

Final default and shadow suites, three-engine parity, no-regression parity, test-hook results, and Agent 2 outcome are pending. Previously documented C1 failures were 3/165: conformance gate 10 (`compiled_only_custom_key`) and the two `swarm_035_nested_mutating_calls.moss` Rust E0499 jobs; final suite results must show no additions. Paired raw performance data: `tmp/compiler-perf/20261102-015109/LOCAL_REPORT.md` and `tmp/compiler-perf/20261102-015509/LOCAL_REPORT.md`.
