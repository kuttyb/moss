# Phase 22.1 post-change baseline

This directory contains the single canonical post-22.1 fresh-agent run over the
unchanged AB001-AB030 corpus. `protocol.json` records the compiler and orchestration
commit, fixed model/runtime settings, isolation, limits, and metric definitions.
Each task directory retains the exact prompt, final relevant workspace, ordered
Moss/Margo tool log, compact manifest, final agent message, and authoritative
validator result.

The raw task artifacts are immutable evidence. Derived files are:

- `summary.json`: runner summary;
- `aggregate.json`: deterministic post-run aggregate;
- `comparison.json`: machine-readable pre/post comparison;
- `REPORT.md`: honest Phase 22.1 comparison and limitations.

Regenerate or verify only the derived files from the repository root:

```sh
python3 benchmarks/agent/compare_baselines.py
python3 benchmarks/agent/compare_baselines.py --check
```

The comparison uses `../pre-22.1/` without modifying it. There is one stochastic
trial per task; the report must not be interpreted as a statistical estimate.
