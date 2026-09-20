# Moss performance benchmarks

Phase 7 benchmarks are top-level Moss declarations compiled and run by
`moss bench`. They use the release/high-optimization compiler path, a fixed
warmup/sampling method, stable project identities, and toolchain-aware saved
baselines.

Start with [Project workflow](PROJECT_WORKFLOW.md) for an end-to-end example.
This document describes the current benchmark contract and its limitations.

## Benchmark declarations

A benchmark has a quoted, nonempty name and a nonempty two-space-indented body:

```moss
fn score(value: int) -> int:
  value * 3 + 1

bench "score":
  score(42)
```

The declaration is top-level. The colon and quoted name are required.
Duplicate benchmark names in one compilation unit are rejected.

### Ordinary function benchmark

```moss
fn mixed(value: int) -> int:
  value * 7 - 3

bench "mixed arithmetic":
  mixed(1234)
```

### Bare value expression

```moss
bench "bare arithmetic value":
  1234 * 7 - 3
```

### Functional pipeline

```moss
bench "bare functional pipeline":
  values = [1, 2, 3, 4]
  values |> map(_ * 2) |> sum
```

All three forms are accepted by the current compiler and are exercised by
[`examples/projects/phase7_demo`](../examples/projects/phase7_demo).

## Discovery and temporary uber-module

Moss discovers benchmarks in every regular `.moss` file recursively below the
configured application source root and `benches/`.

The `benches/` directory is optional. Until modules/imports exist, the target
is one logical global compilation unit:

```text
src/**/*.moss + benches/**/*.moss
```

A benchmark in `benches/arithmetic.moss` can therefore use declarations from
any application source file without imports. Sorted paths are only a loading
detail; duplicate top-level names are rejected globally.

Each project benchmark has a deterministic identity:

```text
bench:<project-relative-source>:<declared-name>
```

Examples:

```text
bench:src/main.moss:score
bench:benches/arithmetic.moss:bare functional pipeline
```

The identity remains the same for the same relative path and name. Moving the
file or renaming the benchmark changes it. Baseline matching uses this full ID,
not just the displayed name.

Benchmark declarations and their generated harness do not enter an ordinary
`moss build` or `moss build --release` application artifact. Benchmark-specific
artifacts live under `build/bench/`.

Domain topology is likewise taken from the application's static composition
prefix. Benchmark files can call application declarations because the target is
one logical uber-module, but a benchmark does not dynamically create or rewire
domain instances. The concrete route graph and its deterministic application
`domain_rank` values are built from the application composition; synchronization
planning is a later compiler phase.

## Running benchmarks

Run every discovered benchmark:

```sh
moss bench
```

Select a subset with one case-sensitive substring filter:

```sh
moss bench score
moss bench 'bench:benches/arithmetic.moss:bare functional'
```

Moss matches the filter against either the declared name or complete stable
ID. It is not a regular expression or glob. `score` can match `normalized
score`; `Score` does not match unless the case also matches.

Filtering determines which benchmark harnesses run, but all discovered source
units are still parsed and statically checked. A compile error in an unmatched
declaration/file can therefore fail a filtered command. With no match, Moss
returns nonzero with `BENCHMARK_CONFIGURATION_ERROR`.

## Compilation profile and dead-code protection

`moss bench` always uses the release/highest-current-optimization path. There
is no debug benchmark mode in Phase 7. The current Rust backend flags are:

```text
--edition=2021 -D warnings -O -C debuginfo=1
```

This matters when comparing benchmark results: `moss bench` measures the same
optimized Moss/Rust profile that `moss build --release` uses rather than
accidentally timing debug code.

The generated harness passes every otherwise discarded value-producing
benchmark expression through a backend black-box boundary. This includes:

- function/method call results;
- bare arithmetic or other value expressions;
- functional pipeline results;
- locals that would otherwise be unobserved at the end of the benchmark body.

The current backend uses `std::hint::black_box`. This prevents the compiler
from trivially deleting the benchmark's result, without exposing an unsafe or
special value in Moss source. It is not a blanket ban on legitimate
optimization inside the expression. Write a benchmark whose inputs and body
represent the work you intend to measure.

Assignments/setup inside a `bench` body execute on every invocation and are
part of the measured work. If a benchmark sends domain messages, its generated
body also waits for tracked work to complete before returning.

## Exact Phase 7 methodology

The methodology is currently fixed rather than manifest-configurable:

| Quantity | Value | Meaning |
| --- | ---: | --- |
| Warmup rounds | 5 | Untimed rounds before measurement. |
| Invocations per warmup round | 1,000 | Calls to the complete benchmark body in each warmup round. |
| Measured samples | 31 | Separate elapsed-time measurements retained for statistics. |
| Invocations per measured sample | 1,000 | Calls timed together, then converted to a per-invocation estimate. |

For each benchmark, Moss therefore performs 5,000 warmup invocations and
31,000 measured invocations. Each measured sample is produced as follows:

1. Start a monotonic elapsed-time measurement.
2. Invoke the complete benchmark body 1,000 times.
3. Measure total elapsed nanoseconds.
4. Divide by 1,000 and round upward to an integer nanosecond.

Moss sorts the 31 per-invocation sample estimates, then reports:

- `p25`: the sample at index `31 / 4` in sorted order;
- `median`: the sample at index `31 / 2`;
- `p75`: the sample at index `(31 * 3) / 4`;
- `samples`: 31.

In plain language, the median is the middle estimate and is the primary result
because one unusually slow sample affects it less than it would affect a mean.
p25 and p75 bound the middle half of this small observed sample set and give a
simple view of variation. They are not confidence intervals.

The harness does not subtract loop/call/timer overhead, pin CPU affinity,
disable frequency scaling, isolate processes, or claim sub-nanosecond
precision. Integer estimates are rounded up. Treat very small readings and
small percentage changes cautiously.

## Human-readable output

Representative output is:

```text
score

median: 812 ns
p25: 798 ns
p75: 829 ns
samples: 31
```

Timing values depend on the program, machine, compiler, and system load. Each
selected benchmark gets its own block. A successful run exits 0. Configuration,
compilation, or benchmark-runtime failures exit nonzero.

## JSON output

Use the shared Phase 6A envelope for CI and agents:

```sh
moss bench --json
moss bench 'bare functional' --json
```

The result contains the reported statistics, methodology counts, backend
identity, optional comparisons/warnings, and regression state. This is the
current shape; paths, timing values, compiler version, and fingerprint are
illustrative:

```json
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0",
  "command": "bench",
  "ok": true,
  "result": {
    "project": "phase7-demo",
    "profile": "release",
    "filter": "bare functional",
    "benchmarks": [
      {
        "id": "bench:benches/arithmetic.moss:bare functional pipeline",
        "name": "bare functional pipeline",
        "source_file": "/work/phase7-demo/benches/arithmetic.moss",
        "line": 10,
        "median_ns": 8,
        "p25_ns": 8,
        "p75_ns": 9,
        "samples": 31,
        "warmup": 5,
        "iterations": 1000
      }
    ],
    "backend_toolchain": {
      "fingerprint": "0123456789abcdef",
      "resolved_rustc": "/usr/bin/rustc",
      "version_verbose": "rustc <installed version and verbose identity>",
      "profile": "release",
      "compile_flags": [
        "--edition=2021", "-D", "warnings", "-O", "-C", "debuginfo=1"
      ]
    },
    "baseline_compatible": null,
    "comparisons": [],
    "warnings": [],
    "saved_baseline": null,
    "regression": false,
    "diagnostic": null
  }
}
```

`iterations` is the number of body invocations per warmup/measured round, not
the total measured invocation count. Raw sample values are persisted in saved
baseline files but are not included in the ordinary `moss bench --json`
result. See [Agent API](AGENT_API.md) for the common envelope/error contract.

## Saving a baseline

Run the benchmarks and save their results under a name:

```sh
moss bench --save baseline
```

Output ends with:

```text
Saved baseline 'baseline'.
```

Moss writes:

```text
.moss/benchmarks/baseline.json
```

Saving again with the same name replaces that file. A filter may be combined
with `--save`; the resulting baseline contains only the selected benchmarks:

```sh
moss bench score --save score-before-change
```

Baseline names may contain only letters, digits, `.`, `-`, and `_` and must be
nonempty. Baselines are retained by `moss clean` because they are comparison
data, not build cache.

The current baseline format (`moss-benchmark-baseline`, version 2) records:

- Moss compiler version and release profile;
- platform identity (operating system, kernel release, and machine
  architecture as reported by the host);
- resolved Rust compiler and complete `rustc --version --verbose` identity;
- effective backend profile, flags, and fingerprint;
- UTC save timestamp;
- stable benchmark ID/name;
- median, p25, p75, warmup, sample, and iteration counts;
- all sorted per-invocation sample values.

The file is JSON intended to be read and validated by Moss. Some variable
strings are losslessly hex-encoded in the persisted format, so prefer
`moss bench --compare` or `--json` over hand-editing it.

Legacy version-1 baseline files can still be read, but they do not contain the
required Rust backend identity and are therefore treated as incompatible.
Record a new version-2 baseline in the intended toolchain environment.

## Comparing a baseline

```sh
moss bench --compare baseline
```

Moss first performs a new release benchmark run, then matches current and
baseline entries by stable benchmark ID. A compatible match produces:

```text
score
baseline: 812 ns
current: 745 ns
change: -8.3%
```

The percentage uses the medians:

```text
(current median / baseline median - 1) * 100
```

Negative is faster and positive is slower. The comparison output is in
addition to the current run's ordinary median/p25/p75 block.

With `--json`, each matching comparison contains exactly:

```json
{
  "id": "bench:src/main.moss:score",
  "baseline_ns": 812,
  "current_ns": 745,
  "change_percent": -8.251
}
```

Only IDs present in both the selected current run and the baseline are
compared. A compatible baseline with no matching current IDs emits a warning
and produces no numeric comparisons.

## Regression thresholds

Fail when any matched benchmark is more than a chosen percentage slower:

```sh
moss bench --compare baseline --fail-over 5%
```

The `%` suffix is optional:

```sh
moss bench --compare baseline --fail-over 5
```

Rules:

- the threshold must be a finite, nonnegative number;
- `--fail-over` requires `--compare <name>`;
- the check is strictly `change > threshold`;
- at least one regression makes the command exit nonzero (currently 1);
- human output includes `error[BENCHMARK_REGRESSION]`;
- JSON sets `ok: false`, `regression: true`, and uses diagnostic code
  `BENCHMARK_REGRESSION`.

The threshold is optional by design. Timing noise can easily make a rigid
threshold misleading; choose it only with knowledge of the benchmark and
environment.

## Compatibility safeguards

Moss performs numeric comparison and threshold enforcement only when these
recorded facts agree:

- Moss compiler version;
- release profile;
- platform identity;
- presence of Phase 7's backend-toolchain metadata;
- resolved `rustc` path;
- complete `rustc --version --verbose` output;
- backend profile and effective Rust flags/fingerprint.

If they do not agree, Moss emits a `BASELINE_INCOMPATIBLE` warning, leaves
comparisons empty, and skips `--fail-over` enforcement. Incompatible comparison
alone is a warning and does not turn a successful benchmark run into a failure.
There is currently no force-compare option.

This safeguard prevents an old executable or baseline from silently spanning a
different backend compiler. It cannot prove that two runs had equal CPU load,
thermal state, frequency, background activity, firmware, or microarchitecture.
The recorded platform identity is not a complete hardware inventory. For
meaningful comparisons, use the same machine and controlled conditions in
addition to satisfying Moss's compatibility check.

## Writing useful benchmarks

- Benchmark one clear operation per declaration.
- Remember that every statement in the benchmark body is timed; move one-time
  data preparation out only when the current source/compilation-unit model can
  represent that honestly.
- Avoid `echo`, external I/O, and unrelated domain work unless those effects
  are the subject of the measurement.
- Compare medians and inspect p25/p75 rather than interpreting one run as
  exact.
- Repeat surprising measurements and control machine load.
- Use stable names and paths if results must continue matching an existing
  baseline.
- Do not use automated tests that depend on real-time nanosecond thresholds;
  test harness structure and methodology deterministically instead.

## Troubleshooting

| Code | Meaning |
| --- | --- |
| `BENCHMARK_CONFIGURATION_ERROR` | No benchmark exists/matches, arguments or baseline name are invalid, a baseline is missing/malformed, or units select inconsistent backend identities. |
| `BENCHMARK_RUNTIME_ERROR` | The native harness failed or emitted invalid/incomplete measurement records. |
| `BASELINE_INCOMPATIBLE` | Warning: recorded compiler/profile/platform/backend facts differ, or no selected IDs can be compared. |
| `BENCHMARK_REGRESSION` | A compatible matched median exceeded the requested threshold. |
| `BUILD_TOOL_NOT_FOUND` / `BUILD_BACKEND_ERROR` | Rust could not be found, identified, or used to build a benchmark unit. |

Common checks:

- Filters are case-sensitive substrings of the name or full ID.
- External benchmark helpers must be in the same `.moss` file.
- `--fail-over` cannot be used without `--compare`.
- Save a new baseline when intentionally changing compiler/toolchain/platform;
  do not expect a threshold to apply to an incompatible baseline.
- Run project commands sequentially because Phase 7 does not lock artifact
  directories against concurrent writers.
- `moss clean` removes `build/bench` but intentionally leaves saved baselines.
