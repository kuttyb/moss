# Moss Pre-22.1 Fresh-Agent Baseline

## Protocol

This report aggregates the frozen `pre-22.1` baseline using schema `moss-agent-baseline-aggregate-1`. The corpus commit is `b187009f6ef06738c52216d34490342d76d7ab4c`, the compiler commit is `22fdb024e4f284d17fb9abfe887856e17127aae7`, and the orchestration commit is `532dc4610d156946df5014573e5d69947f4d3546`. The recorded agent is OpenAI Codex CLI codex-cli 0.156.0 with `gpt-6-sol` at `medium` reasoning. Each task had one fresh session and a 900-second wall limit.

Reference solutions, usable Git history, external web access, and shell network access were unavailable to agents. Final pass/fail came from the authoritative validator after each agent stopped.

## Executive Summary

The baseline passed **28/30 tasks (93.3%)**. There were 2 agent/task failures and 0 infrastructure failures. The first meaningful Moss/Margo correctness command succeeded in **17/30 tasks (56.7%)**. All 30 valid sessions reached a green correctness command.

The 2 final failures were exact-output formatting mismatches after successful compilation: AB014, AB017 printed the correct values on separate lines instead of one space-separated line. No diagnostic-bearing task failed to reach green, and all 10 repair tasks ultimately passed. This baseline therefore supports diagnostic improvements aimed at reducing diagnosis effort and tool calls more strongly than it supports a claim that current diagnostics prevent convergence.

## Overall Results

| Metric | Result |
|---|---:|
| Valid tasks | 30 |
| Final pass | 28 (93.3%) |
| Final fail | 2 |
| Infrastructure failures | 0 |
| First validation green | 17 (56.7%) |
| Eventually green | 30 |
| Never green | 0 |
| Attempts to green, median | 1 |
| Attempts to green, mean | 1.43 |
| Attempts to green, max | 2 |
| Out-of-scope modification tasks | 0 |
| Agent stopped but validator failed | 2 |

Wall-clock values are retained per task but are not treated as a score because runtime load is noisy.

## Results by Category

| Category | Tasks | Pass | Fail | First green | Attempts median / mean / max | Diagnostics | Major tool use | Failure pattern |
|---|---:|---:|---:|---:|---|---|---|---|
| basic-language | 5 | 5 | 0 | 5 | 1 / 1 / 1 | none | moss check (6), moss agent bootstrap (5), moss run --interp (5), moss fmt (2) | none |
| domains-messages | 5 | 5 | 0 | 2 | 2 / 1.6 / 2 | MOSS_COMPILE_ERROR (2), TYPE_INFERENCE_FAILED (1) | moss check (8), moss agent bootstrap (5), moss fmt (5), moss run --interp (5) | none |
| fast-debug | 2 | 2 | 0 | 2 | 1 / 1 / 1 | none | moss check (4), trace (4), moss agent bootstrap (2), moss fmt (2) | none |
| functional-dataflow | 5 | 4 | 1 | 1 | 2 / 1.8 / 2 | FUNCTIONAL_SEMANTIC_ERROR (3), TYPE_INFERENCE_FAILED (1) | moss check (12), moss fmt (8), moss run --interp (7), moss agent bootstrap (5) | exact-output-format-mismatch (1) |
| modules-margo | 3 | 3 | 0 | 2 | 1 / 1.33 / 2 | UNKNOWN_SYMBOL_OR_TYPE (1) | moss check (4), margo run (3), moss agent bootstrap (3), margo clean (1) | none |
| ownership-effects | 5 | 4 | 1 | 3 | 1 / 1.4 / 2 | QUERY_TARGET_NOT_FOUND (2), FORMAT_CHECK_FAILED (1), OWNERSHIP_CONFLICTING_ACCESS (1) | moss check (8), moss effects (7), moss fmt (7), moss agent bootstrap (5) | exact-output-format-mismatch (1) |
| synchronization | 3 | 3 | 0 | 0 | 2 / 2 / 2 | OWNERSHIP_CONFLICTING_ACCESS (3), MOSS_COMPILE_ERROR (2) | moss check (6), moss agent bootstrap (3), moss run --interp (3), moss effects (2) | none |
| tooling-navigation | 2 | 2 | 0 | 2 | 1 / 1 / 1 | none | margo test (2), moss agent bootstrap (2), margo build (1) | none |

The categories with 100% final pass rates were basic-language, domains-messages, fast-debug, modules-margo, synchronization, tooling-navigation. Final failures occurred only in functional-dataflow, ownership-effects. Synchronization had 0/3 first-validation successes and functional/dataflow had 1/5, but those groups contain intentionally broken repair/workflow starters; every task reached green in at most two correctness attempts. These first-attempt rates measure agent workflow as well as source fluency and should not be read as category failure rates.

## Results by Task Type

| Task type | Tasks | Pass | Fail | First green | Attempts median / mean / max |
|---|---:|---:|---:|---:|---|
| repair | 10 | 10 | 0 | 2 | 2 / 1.8 / 2 |
| workflow | 10 | 10 | 0 | 6 | 1 / 1.4 / 2 |
| write | 10 | 8 | 2 | 9 | 1 / 1.1 / 2 |

All 10 repair and 10 workflow/debug tasks passed. Write tasks passed 8/10; the write-task failures compiled and ran but missed exact output shape. Repair tasks were first-green only 2/10 because most agents compiled the supplied broken program before editing it, then recovered on the next correctness command.

## First-Attempt Performance

- First attempt green (17): AB001, AB002, AB003, AB004, AB005, AB006, AB007, AB011, AB014, AB015, AB016, AB024, AB026, AB027, AB028, AB029, AB030.
- Eventually green after an initial failure (13): AB008, AB009, AB010, AB012, AB013, AB017, AB018, AB019, AB020, AB021, AB022, AB023, AB025.
- Never green (0): none.

AB014 was compiler-valid on its first correctness command but failed final output validation. AB017 recovered from TYPE_INFERENCE_FAILED and reached green, then failed the same output-shape requirement. Those cases separate Moss/compiler fluency from task-comprehension and validator compliance.

## Diagnostics and Recovery

| Diagnostic ID | Tasks | Occurrences | Before first green and recovered | Final pass | Additional attempts to green, median | Task IDs |
|---|---:|---:|---:|---:|---:|---|
| `MOSS_COMPILE_ERROR` | 4 | 4 | 4 | 4 | 1 | AB008, AB009, AB021, AB023 |
| `OWNERSHIP_CONFLICTING_ACCESS` | 2 | 4 | 2 | 2 | 1 | AB012, AB022 |
| `FUNCTIONAL_SEMANTIC_ERROR` | 3 | 3 | 3 | 3 | 1 | AB018, AB019, AB020 |
| `QUERY_TARGET_NOT_FOUND` | 1 | 2 | 0 | 1 | n/a | AB015 |
| `TYPE_INFERENCE_FAILED` | 2 | 2 | 2 | 1 | 1 | AB010, AB017 |
| `FORMAT_CHECK_FAILED` | 1 | 1 | 0 | 1 | n/a | AB013 |
| `OWNERSHIP_USE_AFTER_CONSUME` | 1 | 1 | 1 | 1 | 1 | AB013 |
| `UNKNOWN_SYMBOL_OR_TYPE` | 1 | 1 | 1 | 1 | 1 | AB025 |

Observed recovery was shallow: all 13 initially failing sessions reached green by correctness attempt 2, and no task had a higher attempts-to-green value. The raw telemetry does not contain source-edit snapshots between commands, so it cannot prove which edit or diagnostic text caused recovery. 12 of these 13 sessions finally passed; AB017's later output mismatch was unrelated to its initial type error.

Only AB022 used semantic queries before reaching green: `moss effects` and `moss ownership`. Other initially failing sessions recorded no semantic query before their first green command. That is an association, not evidence that compiler feedback alone caused the edits.

### Recorded recovery paths

| Task | Diagnostic sequence | Next Moss/Margo tool | First green command | Semantic queries before green | Final |
|---|---|---|---|---|---|
| AB008 | `MOSS_COMPILE_ERROR` | `moss fmt task/main.moss` | `moss check task/main.moss --json` | none | pass |
| AB009 | `MOSS_COMPILE_ERROR` | `moss check task/main.moss --json` | `moss check task/main.moss --json` | none | pass |
| AB010 | `TYPE_INFERENCE_FAILED` | `moss fmt task/main.moss` | `moss run --interp task/main.moss` | none | pass |
| AB012 | `OWNERSHIP_CONFLICTING_ACCESS` | `moss run --interp task/main.moss` | `moss run --interp task/main.moss` | none | pass |
| AB013 | `OWNERSHIP_USE_AFTER_CONSUME` | `moss check task/main.moss --json` | `moss check task/main.moss --json` | none | pass |
| AB017 | `TYPE_INFERENCE_FAILED` | `moss check task/main.moss --json` | `moss check task/main.moss --json` | none | fail |
| AB018 | `FUNCTIONAL_SEMANTIC_ERROR` | `moss run --interp task/main.moss` | `moss run --interp task/main.moss` | none | pass |
| AB019 | `FUNCTIONAL_SEMANTIC_ERROR` | `moss check task/main.moss --json` | `moss check task/main.moss --json` | none | pass |
| AB020 | `FUNCTIONAL_SEMANTIC_ERROR` | `moss fmt task/main.moss --check --json` | `moss check task/main.moss --json` | none | pass |
| AB021 | `MOSS_COMPILE_ERROR` | `moss run --interp task/main.moss` | `moss run --interp task/main.moss` | none | pass |
| AB022 | `OWNERSHIP_CONFLICTING_ACCESS` → `OWNERSHIP_CONFLICTING_ACCESS` → `OWNERSHIP_CONFLICTING_ACCESS` | `moss effects update_and_read --source task/main.moss --json` | `moss check task/main.moss --json` | `moss effects`, `moss ownership` | pass |
| AB023 | `MOSS_COMPILE_ERROR` | `moss check task/main.moss --json` | `moss check task/main.moss --json` | none | pass |
| AB025 | `UNKNOWN_SYMBOL_OR_TYPE` | `moss check task/src/main.moss --json` | `moss check task/src/main.moss --json` | none | pass |

The telemetry can order commands and diagnostic IDs but does not preserve source snapshots for each edit. The table therefore reports association and sequence, not that a particular diagnostic or tool caused recovery.

## Tool Discovery and Usage

`Available` is 30 for each normal agent-facing command. `Explicitly relevant` is deliberately narrow: it counts only tools named by a workflow prompt, including stated alternatives.

| Tool | Available tasks | Explicitly relevant | Used tasks | Invocations | Successful-use tasks | Used before green in recovery |
|---|---:|---:|---:|---:|---:|---:|
| `moss agent bootstrap` | 30 | 1 | 30 | 30 | 30 | 13 |
| `moss check` | 30 | 2 | 27 | 48 | 27 | 13 |
| `moss inspect` | 30 | 0 | 2 | 2 | 2 | 0 |
| `moss type` | 30 | 0 | 0 | 0 | 0 | 0 |
| `moss effects` | 30 | 2 | 4 | 9 | 4 | 1 |
| `moss ownership` | 30 | 1 | 1 | 2 | 1 | 1 |
| `moss calls` | 30 | 0 | 1 | 1 | 1 | 0 |
| `moss why` | 30 | 0 | 0 | 0 | 0 | 0 |
| `moss run --interp` | 30 | 2 | 23 | 25 | 23 | 0 |
| `moss debug` | 30 | 2 | 0 | 0 | 0 | 0 |
| `trace` | 30 | 3 | 2 | 4 | 2 | 0 |
| `margo build` | 30 | 1 | 1 | 1 | 1 | 0 |
| `margo test` | 30 | 2 | 2 | 2 | 2 | 0 |
| `margo run` | 30 | 2 | 3 | 3 | 3 | 0 |
| `margo clean` | 30 | 0 | 1 | 1 | 1 | 0 |
| `moss fmt` | 30 | 0 | 20 | 27 | 20 | 3 |
| `moss help` | 30 | 0 | 1 | 1 | 1 | 0 |
| `moss run` | 30 | 0 | 1 | 1 | 0 | 0 |

Bootstrap was used in all 30 tasks. `moss check` appeared in 27 tasks; interpreter, debug, or trace execution appeared in 25. Effects was used in 4 tasks, ownership in 1, inspect in 2, and calls in 1.
No agent invoked `moss type` or `moss why`; no prompt explicitly required either, so this is evidence of
non-discovery but not proof that either tool would have changed an outcome. Direct `moss debug` was not
used without trace, while both Fast Debug tasks used trace successfully.

### Workflow-task compliance

| Task | Final validator | Explicit tool requirements | Agent used intended tool(s) |
|---|---|---:|---|
| AB015 | pass | 3 | yes |
| AB021 | pass | 1 | yes |
| AB022 | pass | 2 | yes |
| AB023 | pass | 0 | no named tool requirement |
| AB025 | pass | 1 | yes |
| AB026 | pass | 1 | yes |
| AB027 | pass | 1 | yes |
| AB028 | pass | 1 | yes |
| AB029 | pass | 3 | yes |
| AB030 | pass | 1 | yes |

All 9 workflow tasks with an explicit tool requirement used a permitted intended command and passed.
AB023 also passed but its prompt prescribed a source repair rather than a particular diagnostic command.
This separates final behavior from tool compliance without adding tool use to benchmark pass/fail.

## Failure Modes

Failure classes are assigned deterministically from run state, path violations, and authoritative validator failure records. They do not rely on an inferred reading of the agent's prose.

- `exact-output-format-mismatch`: 2 task(s): AB014, AB017.

The only canonical final-failure class across 2 tasks was exact output formatting. There were no invalid final projects,
structural-shortcut failures, out-of-scope edits, timeouts, or infrastructure failures. Both agents exited
normally and reported completion before the authoritative validator rejected their output.

## Repeated Failure Loops

- **AB015 — `QUERY_TARGET_NOT_FOUND` ×2**: `moss effects Read --source task/main.moss --json` → `moss effects Accept --source task/main.moss --json`. The program was already green before this loop. Later green command: `moss run --interp task/main.moss`; final status: pass.
- **AB022 — `OWNERSHIP_CONFLICTING_ACCESS` ×3**: `moss check task/main.moss --json` → `moss effects update_and_read --source task/main.moss --json` → `moss ownership update_and_read --source task/main.moss --json`. The loop occurred before first green. Later green command: `moss check task/main.moss --json`; final status: pass.

AB015 shows query-target discovery friction: bare `Read` and `Accept` targets failed before qualified
`Reader.Accept` and `Store.Read` succeeded. AB022 shows the ownership conflict propagating unchanged
through check, effects, and ownership queries before the source repair. No task cycled through the same
failed correctness command more than once, and no A→B→A diagnostic loop was recorded.

## Phase 22.1 Opportunities

The candidates below are ordered by affected tasks and repeated recorded friction. They are hypotheses
for the later post-22.1 comparison, not changes made by this phase.

| Candidate | Evidence/leverage | Affected tasks | Current diagnostic | Suggested improvement | Expected measurable effect |
|---|---|---|---|---|---|
| Give domain/message restrictions stable, specific diagnostic IDs | 4/30 tasks, 4 recorded diagnostic occurrence(s) | AB008, AB009, AB021, AB023 | `MOSS_COMPILE_ERROR` | Emit rule-specific IDs and name the ordinary-helper or message rewrite appropriate to the rejected call. | Compare diagnosis/tool-call counts on the affected domain and synchronization repairs; all currently require a second correctness attempt. |
| Make overlapping-access diagnostics identify the conflicting argument roles | 2/30 tasks, 4 recorded diagnostic occurrence(s) | AB012, AB022 | `OWNERSHIP_CONFLICTING_ACCESS` | Name the overlapping expressions and inferred WRITE/READ roles, then state that distinct owned values are required. | Reduce the three-diagnostic AB022 query loop and preserve one-step recovery in AB012. |
| Specialize functional-pipeline diagnostics by rejected callable rule | 3/30 tasks, 3 recorded diagnostic occurrence(s) | AB018, AB019, AB020 | `FUNCTIONAL_SEMANTIC_ERROR` | Identify the unsupported callable form or captured effect and show the accepted placeholder/named-callable shape. | Reduce diagnosis effort and tool calls in AB018–AB020; each already recovers by the next correctness attempt. |
| Teach canonical semantic-query target qualification | 1/30 tasks, 2 recorded diagnostic occurrence(s) | AB015 | `QUERY_TARGET_NOT_FOUND` | Return candidate fully qualified targets and the canonical handler:Domain.Name spelling in the diagnostic payload. | Remove the two failed query calls in AB015 without changing task behavior. |
| Add concrete inference context to TYPE_INFERENCE_FAILED | 2/30 tasks, 2 recorded diagnostic occurrence(s) | AB010, AB017 | `TYPE_INFERENCE_FAILED` | Name the unresolved expression, expected type source, and callable/collection context in structured fields. | Measure tool-call and edit-count reduction in AB010 and AB017; final AB017 failure is unrelated output formatting. |

The strongest limitation on headline pass rate was not a compiler diagnostic: the final failures already had
green compilation and execution. Phase 22.1 should therefore use diagnostic-specific measures such as
failed query count, tool calls, and recovery shape alongside final pass rate. The baseline provides no
evidence for changing Moss semantics or accepting additional programs.

## Methodological Limitations

- There is one canonical agent trial per task; model behavior is stochastic.
- Thirty tasks are a small corpus, and category groups contain only two to five tasks.
- Wall-clock time depends on runtime load and is retained as context rather than a primary score.
- Some tasks admit multiple correct implementations. Structural assertions test requested concepts but
  are not formal equivalence proofs.
- The baseline measures the recorded Codex/model/reasoning/runtime configuration. A future model or agent
  upgrade is a confounder and must not be presented as a Moss-only effect.
- Moss/Margo telemetry records commands and diagnostic IDs, not full agent reasoning or source snapshots
  after every edit. Recovery causality and exact edit sequences are therefore unavailable.
- First-validation success includes agents that intentionally compiled an invalid repair starter before
  editing, so it measures workflow behavior as well as source-generation quality.

## Comparison Contract

A post-22.1 comparison must retain AB001–AB030 at corpus revision
`b187009f6ef06738c52216d34490342d76d7ab4c`, the task prompts, generic wrapper hash
`a3ddf0a048d7b36fa502718151f3639f0c2a522f2f318356c8f725547bdd03ea`, one fresh trial per task, the same agent/model/reasoning
configuration where possible, the same isolation and network restrictions, the same per-task budget,
and the same definitions of first validation, attempts to green, final pass, diagnostics, tools, and path
violations. Any unavoidable difference must be identified as a comparison confounder.

The machine-readable source for every table is `aggregate.json`; per-task evidence remains in the adjacent
Phase 22.2C manifests, tool logs, validator results, and final workspaces.
