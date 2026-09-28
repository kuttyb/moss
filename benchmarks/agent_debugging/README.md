# Phase 22.4 agent debugging benchmark

This benchmark measures whether structured Fast Debug queries reduce the dynamic
execution context an agent must inspect to diagnose representative runtime
failures.

It intentionally measures trace volume, not interpreter speed. Each case runs
once with the raw structured trace and once through `moss debug-query`, then
records:

- raw trace event count;
- sliced event count;
- slice/full-trace ratio;
- whether the slice contains the expected failure/control/write/message facts.

Run:

```sh
python3 benchmarks/agent_debugging/run.py ./moss
```

The harness writes its latest JSON result to `tmp/phase224_agent_debugging_results.json`.
All scratch output stays under `tmp/`.

Latest local result for Phase 22.4 closeout:

```text
cases: 6 / 6 succeeded
raw trace events: 177
sliced events: 64
aggregate slice/full-trace ratio: 0.3616
tool calls to slice: 1 per case
```
