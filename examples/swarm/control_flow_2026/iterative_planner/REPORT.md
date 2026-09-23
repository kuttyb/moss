# Iterative Planner dogfood report

## Intent and final behavior

This is an iterative, dependency-aware planning program rather than a branch
probe. It scans candidate tasks until no eligible task remains, evolving a
selection bitmap, remaining effort budget, delivered-value accumulator, and
round counter. The chooser contains a `while` scan, three nested eligibility
branches, a helper call for dependency state, a helper call for priority, and a
best-result accumulator. `main` takes both the success and stop paths.

The final example uses aligned integer vectors for task value, effort,
dependency index, and urgency. With a budget of 10 it selects task indexes 0,
1, and 3 (survey, foundation, launch), then stops when no eligible task fits.
Native and interpreted output is `3 45 1`.

## Fresh-agent procedure

Before source inspection/editing, I read `AGENTS.md`, fully read the
`moss-language` and `moss-agent-workflow` skills, read the practical language
guide sections used here, and ran `./moss agent bootstrap --json` from the
repository root. Bootstrap reported language version `moss-0.1`, the expected
`Vector`/`while`/`if` surface, structured diagnostics, Margo project tooling,
and Fast Debug with trace support. I read the swarm ledger only after the first
meaningful application check/run attempt.

The package-local `./moss`/`./margo` invocation was a non-application path
mistake: the repository tools are at the checkout root. Subsequent commands
used `../../../../moss` and `../../../../margo` from this package directory.

## Natural first formulation and iterations

There were four failed natural application formulations before the final
program, all preserved with their tool output in `failed_attempts/`.

1. `01_multiline_task_literal.moss` used a multi-line vector literal of
   `Task` records. The structured checker and Margo build both rejected line
   45 with `MOSS_COMPILE_ERROR: indentation jumps more than one block level`.
   The formatter agreed. The direct, single-line vector literal was accepted.
   This is a source-layout/parser restriction or syntax-discoverability issue,
   not evidence that vectors or task records are absent.
2. `02_untyped_vector_parameters.moss` passed `selected` and `tasks` through
   untyped helper parameters. `selected[task.dependency]` produced `value is
   not an indexable container`. Adding the concrete `Vector[Int]` and
   `Vector[Task]` parameter types made the intended indexing information
   available. This was inference/surface discovery, not an ownership claim.
3. `03_pipeline_count_in_condition.moss` embedded `tasks |> count` directly in
   comparison conditions. The checker diagnosed `unknown local function
   'count'`; formatter agreed. Binding `task_count = tasks |> count` before the
   comparison keeps the value flow explicit and succeeds. The practical guide
   demonstrates pipelines as an expression, but this diagnostic does not
   identify the comparison/pipeline parsing boundary.
4. `04_vector_task_ownership.moss` was the complete record-oriented planner.
   Its `task = tasks[index]` made `choose_next` consume `Vector[Task]`, so the
   second planning round hit `OWNERSHIP_USE_AFTER_CONSUME` at the call site.
   The checker reports the consumer line and offers the relevant alternatives;
   ownership/effects/calls queries consistently reported the same failed
   program, as they must before a checked semantic target exists.

The final implementation is a deliberate data-layout restructure after that
evidence: primitive indexed task columns permit repeated scans, and the live
ownership/effects query proves all six `choose_next` parameters are `READ`.
It is not presented as a general requirement that Moss planners use parallel
arrays; it is the data layout that preserves this planner's repeated-read
intent under the observed record-vector access contract.

Counts: 4 failed app formulations; parser/layout 1, missing concrete vector
type information 1, expression-shape/precedence 1, and intended ownership 1.
The nested conditionals, loop mutation, indexed primitive reads, indexed
selection writes, result accumulation, scalar helper calls, and `-1` sentinel
all succeeded naturally in the final program.

## Values, mutation, and inference observations

- `var selected`, `remaining`, `delivered_value`, `rounds`, and scan/best
  locals evolve normally inside `while` loops.
- `selected[next] = 1` performs the persistent result mutation; all candidate
  columns remain read-only inputs to `choose_next`.
- The successful semantic facts report `READ` for values, efforts,
  dependencies, urgencies, selected, and remaining. `choose_next` returns
  `Int`; its static call graph resolves only `dependency_done` and `priority`.
- The record-vector draft revealed that reading a non-copy `Task` via indexed
  extraction has a materially different ownership effect from repeated indexed
  reads of `Int` columns. The compiler's use-after-consume message is specific
  and actionable, including the consumed line and legal alternatives.
- A pipeline terminal used as a standalone binding (`task_count = values |>
  count`) is accepted. In contrast, its direct use after `<` was not accepted
  in this formulation. No parenthesized alternative was adopted: the ledger
  already records formatter/native issues for several parenthesized expression
  shapes, so a local count is the most transparent implementation here.

## Validation and tool agreement

All commands below were run from this package directory using checkout-root
tools.

| Check | Result |
| --- | --- |
| `moss fmt --json src/main.moss tests/planner_test.moss` | Canonical; no changes required |
| `moss check src/main.moss --json` | 0 diagnostics |
| `margo build --json` | Native debug artifact built successfully |
| `margo run` | `3 45 1` |
| `margo test --json` | 3 passed, 0 failed |
| `margo debug --trace` | `3 45 1`; structured loop, branch, local read/write, helper entry/exit, and return events emitted |
| `moss inspect/type/effects/ownership/calls/why/impact ... --json` | Successful, consistent static facts |
| `moss test --affected --json` | Correctly selected no tests after the unchanged baseline; full Margo test remains the authoritative 3/3 result |

The trace shows the source planner's first candidate taking the root-dependency
branch, blocked dependent candidates taking the false branch, priority helper
returns, and eventual `next < 0` stop branch. It is an execution trace, not a
claim about scheduling or locks; this program has no domains.

## Tests and reproductions

`tests/planner_test.moss` provides three meaningful checks:

- score calculation distinguishes higher-value work from lower-value work;
- successive chooser calls unlock dependency index 0, then index 1, then the
  high-priority launch task at index 3;
- a self-blocked dependency returns the `-1` no-candidate result.

Every failed natural source and its recorded command output live under
`failed_attempts/01_*` through `failed_attempts/04_*`. The failures were
diagnosed with structured JSON before the next source change. The ownership
case additionally exercised `ownership`, `effects`, and `calls`; since the
program was invalid, each correctly returned the same primary diagnostic
rather than inventing a partial semantic answer.

## Diagnostic quality and ledger overlap

`OWNERSHIP_USE_AFTER_CONSUME` was high quality: it names the consumed value,
consumer line, available-vs-consumed state, and choices. The other three
diagnostics were stable but less explanatory: the multiline-literal message
does not identify the bracketed collection, `value is not an indexable
container` does not suggest concrete vector parameter types, and `unknown
local function 'count'` does not explain the pipeline/comparison parse shape.

After completing independent application work, I reviewed the existing swarm
ledger. The parenthesized-expression formatter issue is already recorded as
SWARM-032, but it is not the same observed failure: this planner's unparenthesized
pipeline-in-comparison was rejected by checking. The record-vector ownership
result is an expected Moss ownership outcome, not a compiler defect. The
multi-line literal and count diagnostics were not minimized into new findings,
because this application has working supported forms and the evidence does not
establish a distinct language or compiler bug. No new ledger entry is proposed.
