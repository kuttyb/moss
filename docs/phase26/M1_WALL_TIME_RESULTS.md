# Phase 26.1 M1 wall-time comparison

Status: preliminary local measurement for the Phase 26.1 exit report. This records three runs of each side on `DESKTOP-0T772KP` (WSL2, 16 logical CPUs) on 2026-10-26. Each scenario is a single command per run; the table reports medians across the three runs.

- Before: clean checkout of `origin/main` at `d109b8f17276f70b591432fd345d57e727f17501`.
- After: `phase26-1-m1` at `ad3276ca7dd9bfbdd98598a47c7b507ce16cfffc` plus the C1 closeout working-tree changes.
- Durable raw rows: [baseline CSV](../../benchmarks/phase26_1/results/m1_wall_times_before.csv) and [candidate CSV](../../benchmarks/phase26_1/results/m1_wall_times_after.csv), with a `repetition` column. Temporary per-run reports were written under `tmp/` during measurement.
- Each side completed 70 scenario rows per run (210 rows each). The candidate met all 70 R-rule targets in each run. The baseline did not meet incremental targets, as expected.
- Timing is wall-clock time for the command, including Margo orchestration and the profiling wrapper.

## Package edit and revert commands

The baseline Margo does not support `margo build --check`; its scenario command is `margo build`, which performs the existing native build and includes Rust/Cargo work. Candidate rows use semantic-only `margo build --check`, so these are useful before/after task-loop measurements but are not command-equivalent.

| Edit | Baseline `margo build` median | Candidate `margo build --check` median | Baseline packages checked | Candidate packages checked |
|---|---:|---:|---:|---:|
| E0, source touch | 1,208.8 ms | 36.5 ms | all packages | 0 |
| E1, body edit, interface unchanged | 1,209.7 ms | 57.6 ms | all packages | edited package |
| E2, inferred interface edit | 1,207.5 ms | 85.8 ms | all packages | edited package plus direct dependents |
| E3, explicit signature edit | 1,217.3 ms | 117.7 ms | all packages | required transitive closure |

The candidate package counts met the targets across the dependency chain, top application, fan-in hub, and generic provider positions. The full per-row counters and wall times are in the durable CSVs listed above.

## Same-command file check

| Command | Baseline median | Candidate median |
|---|---:|---:|
| `moss check <edited file>` | 20.8 ms | 19.5 ms |
| `moss check <reverted file>` | 20.5 ms | 20.1 ms |

These measurements cover the generated M1 files and are not a substitute for the compiler-perf suite's small real-file workloads. The range contains occasional outliers, so use the medians as directional evidence rather than a certification threshold.

A final Phase 26.1 exit report should include this table alongside the final M1 gate result and certified compiler-perf run.
