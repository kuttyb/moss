# Simulation Engine Dogfood Report

## Intent and first formulation

This is a five-day emergency-supply planner, not a branch micro-example.
`simulate` reads demand, supply, and weather schedules; mutates `backlog`,
`fulfilled`, `peak_backlog`, and `day`; calls two helpers; and returns a
resilience score. The first natural form used `for day in range(0, demands |>
count)` for daily traversal.

`moss check --json` accepted it with no diagnostics. Native Margo build then
returned structured `BUILD_BACKEND_ERROR`: lowered Rust referenced an undeclared
`day` at indexed vector accesses and produced an `i64` loop result where `()`
was expected. Fast Debug separately said `Fast Debug does not support for
iteration yet`. Both are preserved before the rewrite in
`failed_attempts/01_for_loop_results.md`; the original source is
`failed_attempts/01_for_loop_main.moss`.

The minimal package in `failed_attempts/01_for_native_reproducer/` reduces this
to an indexed range sum. It checks successfully and fails natively with the same
missing loop variable plus unit-return mismatch. This is an open native-lowering
bug, separate from the documented Fast Debug coverage gap, not a source-checker
rejection or application error.

There were two significant edit → check → repair cycles:

1. Replace native/Debug-incompatible indexed `for range` traversal with an
   equivalent mutable-index `while` loop.
2. Parenthesize `demands |> count` in the `while` comparison after the natural
   unparenthesized form produced `UNKNOWN_SYMBOL_OR_TYPE` for `count`.

The second issue is reduced in
`failed_attempts/02_while_pipeline_condition.moss`. Its diagnostic suggests
declaring `count`, but the live source surface documents `vec |> count`; the
successful `while day < (demands |> count):` repair identifies a precedence
syntax-sugar omission, not missing cardinality or looping support.

## Successful control and value flow

`urgency` nests shortage/weather decisions and takes one of three returns.
`deliverable_supply` applies nested weather reductions, a critical-priority
reserve, and a non-negative clamp. `simulate` carries backlog between days,
separately accumulates fulfilled need and peak backlog, exercises recovered and
still-short paths, and increments a mutable local loop index. The demo covers
ordinary fulfillment, severe-weather shortage, recovery, a second peak, and a
critical reserve; it returns 28.

The `for` → `while` rewrite preserved the supply rules; parenthesizing the count
pipeline was a syntax repair. No domain was introduced because all mutation is
local to one deterministic calculation, so shared state and routing add nothing.

## Inference and semantic evidence

Final `inspect`, `calls`, `effects`, `ownership`, and `why` queries resolve
`simulate` to `Int`, list exactly `urgency` and `deliverable_supply` as direct
calls, and list `main` as caller. All three `Vector[Int]` inputs infer `READ`;
the mutable accumulators are internal. There were no ownership conflicts or
implicit copies. Effects report no domain/message/I/O, `may_fail: true` for
indexing, and conservative `may_diverge: true` for the explicit `while` loop.
That termination conservatism was the only inference surprise.

`moss impact simulate --source src/main.moss --json` reports only `main` as a
dependent. Affected-test selection conservatively ran all four tests because no
prior semantic baseline was available.

## Tool agreement and outputs

| Tool | Result |
| --- | --- |
| `moss fmt` then `moss fmt --check --json` | Canonical; no further changes |
| `moss check` on source and tests | Clean |
| `moss test --affected --json` | 4/4 pass |
| `margo build --json` | Native build succeeded |
| `margo run` | `resilience score 28` |
| `margo test --json` | 4/4 pass |
| `margo debug` | `resilience score 28` |
| `margo debug --trace` | Completed; five `LoopIteration` events, final return 28 |

The trace records local writes for all four mutable simulation bindings, indexed
schedule reads, both helper entries/exits, nested branches, and final `Return`
28. Checker, formatter, native execution, and Fast Debug agree for the final
`while` implementation.

The first native diagnostic correctly classifies `BUILD_BACKEND_ERROR` but lacks
a Moss source span and exposes generated-Rust details. The Fast Debug limitation
is direct and actionable. The pipeline diagnostic is structured but its suggested
declaration repair misses the actual precedence fix.

## Tests, reproducers, and ledger overlap

All four passed in both affected and native test runs:

- `urgency follows nested shortage and weather rules`
- `delivery applies weather reduction and critical reserve`
- `five day plan tracks backlog peak and recovery`
- `unresolved backlog lowers the final resilience score`

`failed_attempts/01_for_native_reproducer/` is the retained native reproducer;
`failed_attempts/02_while_pipeline_condition.moss` is the checker reproducer.
After completing the first application execution, I searched the shared swarm
ledger. It has no exact entry for checker-accepted indexed `for range` lowering
without its induction binding. `SWARM-042` is only a related native-lowering
warning class for conditionally reassigned initialized locals, not this failure.
The ledger was not modified.

## Workflow reflection

Bootstrap source-surface discovery, structured diagnostics, semantic queries,
Margo package commands, and tracing made the classifications concrete without
using generated Rust as a semantic oracle. The most useful improvement would be
a native-lowering diagnostic that maps loop failures back to the Moss `for` span
and names the missing induction binding directly.
