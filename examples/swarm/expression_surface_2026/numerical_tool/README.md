# Numerical Tool

A small deterministic numerical-analysis package for paired integer sensor
channels. It computes a scaled alert-energy score, peak minute, alert baseline
total, mean observation, calibrated first sample, and high-energy flag.

The program intentionally exercises arithmetic, comparisons, nested helper
calls, `Calibration` field projection, primitive-vector indexing, typed empty
collection construction, mutation, and a `filter |> sum` pipeline.

From this directory:

```sh
../../../../moss fmt src/numerical_tool.moss --check --json
../../../../moss check src/numerical_tool.moss --json
../../../../margo build --json
../../../../margo run
../../../../margo test --json
../../../../margo debug --trace
```

Expected demo output is:

```text
6800
3
20
13
11
true
```

See [REPORT.md](REPORT.md) for the dogfood record and
[`failed_attempts/`](failed_attempts/) for preserved source and diagnostics.
