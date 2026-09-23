# Control-flow and value-flow Moss dogfood swarm — coordinator report

Date: 2026-09-23

## Method

Three agents independently built complete packages before comparing their work
with the SWARM ledger:

| Workload | Natural control-flow focus | Final validation |
| --- | --- | --- |
| [Workflow state machine](workflow_state_machine/) | transitions, retries, nested decisions, early completion/rejection, `break`, mutable event history | 6/6 native tests; native and traced Fast Debug agree |
| [Iterative planner](iterative_planner/) | repeated candidate scan, dependency branches, best-result accumulation, stop sentinel, indexed mutation | 3/3 native tests; native and traced Fast Debug agree |
| [Emergency-supply simulation](simulation_engine/) | five-day local state evolution, nested rules, mutable accumulators, loop progression and recovery paths | 4/4 native tests; native and traced Fast Debug agree |

Every agent performed the fresh Moss bootstrap/skills/documentation workflow,
used structured diagnostics and semantic queries, preserved the application
failure before a workaround, and ran formatter, checker, Margo, Fast Debug, and
trace validation. No compiler, global test, language, or API changes were made.

## What worked naturally

The final programs provide strong positive evidence for ordinary `while`-based
control flow. Mutable local scalars and vectors evolved across many loop
iterations; nested conditionals selected distinct state transitions; helpers
returned values through several logical paths; early function results and `break`
behaved consistently; and loop-produced accumulators fed subsequent results.
Native and Fast Debug agreed on all final demos and all 13 final tests.

Semantic evidence also matched the source intent: the simulation's three input
vectors and the planner's primitive task columns infer `READ`, while mutable
accumulators remain local. The workflow evaluator's event input is likewise
`READ` while its history mutates locally before a terminal calculation.

## Where ordinary algorithms required restructuring

| Observation | Independent evidence | Classification |
| --- | --- | --- |
| Repeated scans of `Vector[Task]` after `task = tasks[index]` consume the record vector | iterative planner | Intended ownership model with a clear `OWNERSHIP_USE_AFTER_CONSUME` diagnostic. The planner switched to primitive columns only after the application demonstrated the repeated-read need. |
| Concrete vector/record parameter types were needed for indexed helper logic | iterative planner | Inference boundary/documentation friction, not a backend discrepancy. |
| `value < values |> count` in a loop condition parsed as an unknown `count`; `(values |> count)` or a bound count works | planner and simulation | Repeated precedence/syntax discoverability friction. The checker diagnostic suggests declaring `count`, not the actual grouping repair. Not established as a language defect. |
| Nested `return -100` checks and executes but formatter rejects it with type-inference wording | workflow state machine | Existing SWARM-033 formatter family, independently expanded from handler `reply -1` to nested ordinary returns. |

## New control-flow parity defect: indexed `for range`

The supply simulation first naturally used an indexed daily traversal. Its
application failure and a minimal package are retained at
[`01_for_native_reproducer`](simulation_engine/failed_attempts/01_for_native_reproducer/).
Coordinator revalidation found:

1. `moss fmt --check` succeeds without changes.
2. `moss check` succeeds with no diagnostics.
3. Native `margo build` fails with `BUILD_BACKEND_ERROR`: generated Rust loses
   the induction binding (`index` is undeclared) and also returns `i64` where
   the loop body requires `()`.
4. Fast Debug rejects the same program with its documented lack of `for`
   iteration support.

The Fast Debug limitation is an existing coverage boundary, but the
checker-accepted native failure is a distinct native-lowering defect. Allocated
as **SWARM-066** in the ledger (`examples/swarm/FINDINGS.md`). The
expression-surface swarm's boolean pipeline failure is allocated as **SWARM-065**.
The application's `while` rewrite is a bounded workaround, not a claim that `for`
is unsupported Moss source.

## Answer to the swarm question

**Partly, but not reliably across the full advertised surface.** Moss handles
realistic `while` loops, mutable local state, nested paths, early values,
accumulators, and helper result flow well once code is within its explicit
ownership and inference boundaries. Yet a source form explicitly advertised by
the bootstrap (`for i in range(...)`) can pass checking and formatting then fail
both execution engines. In addition, ordinary pipeline values in comparison
conditions require undocumented grouping, and nested negative literal returns
still violate formatter/checker agreement. A programmer therefore still has to
restructure some ordinary control flow around toolchain limitations.

Detailed independent evidence is in the three workload reports:

- [Workflow state machine report](workflow_state_machine/REPORT.md)
- [Iterative planner report](iterative_planner/REPORT.md)
- [Simulation engine report](simulation_engine/REPORT.md)
