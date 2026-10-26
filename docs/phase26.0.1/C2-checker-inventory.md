# Phase 26.0.1 C2: checker invocation inventory

## Baseline evidence (main `274f848`)

Phase 26's work-counter CSV reports these candidate-side checker records:

| Workload | Functions/types | Record 1 rounds | Record 2 rounds | Reported aggregate |
|---|---:|---:|---:|---:|
| Five-line example (`example_functional_pipeline`) | 1 / 0 | 5 | — | 5 |
| Regex | 49 / 4 | 5 | 10 | 15 |
| Datastructures | 112 / 11 | 5 | 10 | 15 |

The last column is the CSV's sum of record values. For regex and datastructures,
the second `MOSS_PROFILE_TRACE` group is cumulative: its `inference_rounds=10`
includes the first group's five rounds. The observed checker executions are
therefore two, each performing five signature-inference rounds (10 total), not
three groups or 15 rounds. Each group analyzed the same function/type counts and
reported the same parse-cache totals.

## Invocation inventory

| Checker | Purpose | Work | Duplicate relationship |
|---|---|---|---|
| Project check in `check_project_semantics` | Establish the `moss check` verdict over the selected project source closure. | Parse, resolve, infer, check effects/ownership, validate, optimize the checked program. | Required user-visible check; preserve its source scope. |
| Snapshot analysis in `analyze_project_snapshot` | Build and persist the `.moss/semantic-cache-v1` snapshot after a successful project check. | Previously repeated project parsing and semantic checking, then extracted snapshot facts. On regex and datastructures, the source set is exactly the same single application source. | Pure duplicate for a successful one-source Application check with no source overrides. The snapshot can use the already checked Program after matching the previous optimized projection and native-lowering validation. |

## C2 decision and implementation

The counters justified removing only the duplicate semantic check used to
create a snapshot when the checked source set is exactly the requested single
source in Application mode, with no source overrides. The code preserves
`moss check` scope and the snapshot side effect. Broader source sets, tests,
benchmarks, queries and standalone paths keep their existing behavior.

Snapshot compatibility was checked byte-for-byte at the same paths against
`274f848` for regex and datastructures in default mode and for a pipeline-rich
`examples/functional_pipeline.moss` project with `moss check -O`. The copied
Program is optimized exactly once: default checks optimize the snapshot copy;
`-O` checks reuse their already-optimized Program. The existing const
`generate_native` validation still runs before the snapshot is written, with
snapshot failures remaining non-fatal to the successful check.

The W2 counter test now asserts exactly one checker record for regex and
datastructures. This is a count check; it does not change `moss check` scope.

## Before/after results

Paired local suite command:

```sh
MOSS_PERF_REPS=5 MOSS_PERF_INCLUDE_REAL_WORLD=1 \
  MOSS_PERF_BASELINE=274f848 TMPDIR="$PWD/tmp" make compiler-perf
```

Run `20261026-141926`, baseline `274f848502df08f62b69c54c614ad9f05a90a3ec`,
77 fixtures, 5 interleaved timing repetitions and one separate profile sample,
on the same 16-logical-CPU WSL2 host. Aggregate per-fixture wall medians were
14.800 s baseline and 13.770 s candidate (-7.0%). No fixture crossed the
suite's slowdown noise floor. `w6_types_count_t025` was +1.8% (17.29 vs 16.98
ms), below the threshold; `example_checkout` was 3.90 vs 3.78 ms (-3.1%).

| Attribution workload | Baseline | Candidate | Change |
|---|---:|---:|---:|
| `example_checkout` | 3.90 ms | 3.78 ms | -3.1% |
| `rw_packages_regex` | 1,472.53 ms | 754.81 ms | -48.7% |
| `rw_projects_rolling_window` | 94.70 ms | 51.14 ms | -46.0% |
| `rw_sdk_datastructures` | 563.30 ms | 299.14 ms | -46.9% |
| `w2_spec_distinct_n032` | 1,893.80 ms | 1,920.59 ms | +1.4% |
| `w3_spec_repeated_n032` | 1,001.03 ms | 989.20 ms | -1.2% |
| `w4_infer_deep_d018` | 7,193.15 ms | 7,187.13 ms | -0.1% |
| `w5_infer_wide_w100` | 134.26 ms | 132.50 ms | -1.3% |

| Workload | Baseline checker profiles and rounds | Candidate checker profiles and rounds |
|---|---:|---:|
| Regex (49 functions, 4 types) | 2 profiles: 5, then cumulative 10 | 1 profile: 5 |
| Datastructures (112 functions, 11 types) | 2 profiles: 5, then cumulative 10 | 1 profile: 5 |

For regex, the two baseline profiles each reported 540,689 parse-cache misses;
the candidate's single profile reported 540,689. Datastructures had 293,371
misses per baseline profile and once in the candidate. The saved work is the
second checker execution, not a change to parsing or inference within the
remaining checker.

## Gates

- Full corpus differential: 2,064 sources (1,325 accepted, 739 rejected); zero
  verdict or diagnostic-code differences and zero generated-Rust hash
  differences using full SHA-256 hashes.
- Fast Debug parity: `tests/shards/shard_fast_debug.sh` passed, including the
  whole-project closure, query, and native/Margo parity checks.
- `make --jobs="$(nproc)" check`: 140/140 jobs passed after the final code change.
- Architect: GO for implementation SHA
  `ba859c04d01688aeb14653668dd710242117f804`.

The result supports this narrowly scoped reuse. Residual snapshot work remains:
the program is copied and native Rust is generated for the existing lowering
validation before writing the cache.
