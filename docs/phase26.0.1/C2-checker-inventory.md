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
| Snapshot analysis in `analyze_project_snapshot` | Build and persist the `.moss/semantic-cache-v1` snapshot after a successful project check. | Repeats project parsing and semantic checking, then extracts snapshot facts. On regex and datastructures, the source set is exactly the same single application source. | Likely pure duplicate on the measured one-source Application projects if the existing checked `Program` and `OptimizationPlan` yield byte-identical snapshot contents. |

## C2 decision

The counters justify removing only the duplicate semantic check used to create a
snapshot when the checked source set is exactly the requested single source in
Application mode. Keep writing the same snapshot from the already checked
program. Preserve the current re-analysis path for broader source sets or other
modes unless equivalence is demonstrated. This keeps `moss check`'s scope and
snapshot side effect intact.

Before changing code, verify the snapshot bytes against baseline `274f848` for
regex and datastructures. If they differ or the measured timing/counters do not
improve, make no change or revert it. Full C2 parity and compiler gates remain
pending.
