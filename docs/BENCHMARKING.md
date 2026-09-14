# Moss performance benchmarks

Phase 7 benchmarks are first-class Moss declarations:

```moss
fn score(value: int) -> int:
  value * 3 + 1

bench "score":
  score(42)
```

Run all benchmarks or a filtered subset:

```sh
moss bench
moss bench score
moss bench --json
```

Benchmarks always use the release/highest-current-optimization profile. The generated
harness uses `std::hint::black_box` at the backend boundary for every discarded
value-producing benchmark expression—including calls, arithmetic, and functional
pipelines—performs five warmup rounds,
collects 31 samples with 1,000 invocations per sample, sorts them, and reports integer
nanosecond p25, median, and p75 values. Median is the primary number. The per-invocation
integer value is rounded upward; these measurements are comparative evidence, not a
claim of sub-nanosecond precision.

Moss discovers benchmarks in the configured source and recursively in
`benches/*.moss`. IDs are `bench:<project-relative-source>:<name>`. Like external test
files, files under `benches/` are independently compiled until Moss gains a deliberate
module/import design. Benchmark declarations do not enter normal application artifacts.

## Baselines

```sh
moss bench --save baseline
moss bench --compare baseline
moss bench --compare baseline --fail-over 5%
```

Baselines are inspectable JSON under `.moss/benchmarks/<name>.json`. They retain the
Moss compiler version, release profile, platform identity, resolved `rustc`, complete
`rustc --version --verbose` identity, effective backend flags/fingerprint, UTC timestamp,
stable benchmark ID, summary statistics, methodology counts, and all samples.
Comparison matches by stable ID and reports percentage change only when the environment
and backend fingerprint are compatible. Moss emits a structured
`BASELINE_INCOMPATIBLE` warning when compiler, profile, platform, backend toolchain, or
IDs do not match. An incompatible Rust backend suppresses the comparison and any
`--fail-over` regression enforcement; Moss never silently applies a threshold across
different toolchains. For a compatible baseline, `--fail-over` returns nonzero with
`BENCHMARK_REGRESSION` when a matched median exceeds the requested percentage.
Automated compiler tests validate structure and methodology but never depend on a
real-time threshold.

`moss bench --json` uses the same `moss-agent-1` envelope as diagnostics, semantic
queries, builds, and tests. Benchmark configuration and execution failures use
`BENCHMARK_CONFIGURATION_ERROR` and `BENCHMARK_RUNTIME_ERROR`.
