# Phase 22.3 semantic-query A/B benchmark

This suite measures one intervention: whether a fresh coding agent can access the
Phase 22.3 semantic-query and discovery expansion. Both profiles run the same
checked-out `moss` and `margo` binaries at one Git commit.

The corpus is separate from the frozen `AB001`–`AB030` benchmark. It contains 20
tasks under `tasks/`: four each for resolution, types/ownership, effects/calls,
domains/synchronization, and negative controls. The first 16 request exact
compiler-owned facts. The final four should need no semantic query. Every task
has a starter tree, reviewed expected tree, declarative validator, and
`semantic_ground_truth.json`. Agents must write `task/semantic-answer.json`; the
scorer compares only the requested exact fields.

## Profiles

- `legacy` stages documentation and skills from post-22.1 commit
  `bb388d4aeb1df7f6dac682b427d666a44a748bf4`. The benchmark-only proxy rejects
  `resolve`, removes Phase 22.3 discovery contracts, and projects successful
  query JSON to the fields emitted by that historical implementation.
- `phase22_3` stages current docs/skills and passes current query/discovery JSON
  through unchanged.

The legacy condition emulates the post-22.1 externally visible semantic-query
contract over the current compiler. It does not execute the historical
post-22.1 query implementation. Compiler-internal resolution improvements
shared by both profiles can therefore reduce the measured treatment effect.

The proxy does not gate parsing, checking, ownership rules, lowering,
synchronization analysis, Fast Debug, native execution, or Margo. Profile tests
compare semantic acceptance and execution under both conditions.
Each profile also stages the matching `AGENTS.md` and
`.codex/CURRENT_STATUS.md`, so mandatory startup never falls through to a file
that is absent or belongs to the other condition.

## Validation and smoke run

```sh
python3 benchmarks/agent/phase22_3_ab/runner.py validate
python3 tests/tooling/check_phase22_3_ab.py ./moss

python3 benchmarks/agent/phase22_3_ab/baseline.py run-all \
  --tasks P223001 P223006 P223017 \
  --trials 1 \
  --profiles legacy phase22_3 \
  --output-root tmp/phase22_3_ab-smoke

python3 benchmarks/agent/phase22_3_ab/analyze.py \
  --runs-root tmp/phase22_3_ab-smoke
python3 benchmarks/agent/phase22_3_ab/compare.py \
  --runs-root tmp/phase22_3_ab-smoke
```

Each trial uses a fresh mount namespace and ephemeral Codex context. Git history,
expected solutions, other trials, plugins, browser tools, memories, multi-agent,
and shell network access are unavailable. The legacy and treatment run order is
deterministically shuffled. A completed trial is never silently rerun; restarting
`run-all` fills only missing trial IDs and rejects protocol drift.

## Canonical run

Commit the framework and corpus first. From that frozen commit run:

```sh
python3 benchmarks/agent/phase22_3_ab/baseline.py run-all \
  --trials 5 \
  --profiles legacy phase22_3
```

The command prints the task/profile/trial plan before launching all 200 agent
runs. It refuses to combine different compiler binaries, commits, models,
reasoning settings, timeouts, prompts, starters, or validators. Raw artifacts are
stored as `runs/<profile>/<task>/trial-NNN/` and include the prompt, manifest,
tool log, complete agent event stream, semantic answer, final source tree,
validator result, and last message.

Generate and check derived artifacts with:

```sh
python3 benchmarks/agent/phase22_3_ab/analyze.py
python3 benchmarks/agent/phase22_3_ab/analyze.py --check
python3 benchmarks/agent/phase22_3_ab/compare.py
python3 benchmarks/agent/phase22_3_ab/compare.py --check
```

The analysis reports semantic fact accuracy, validator completion, semantic
detour cost, source/doc/`.mossi`/generated-Rust reads, query success and adoption,
validation attempts, diagnostics, tool calls, Moss/Margo calls, wall time, and
token usage. The paired comparison includes per-task deltas, means, medians,
standard deviations, and deterministic paired bootstrap 95% intervals. These are
descriptive measurements of agent observability, not changes to Moss semantics.
Semantic detour cost applies only to the 16 semantic-heavy tasks: it counts
relevant source inspection, failed/ambiguous/missing queries, and speculative
validation before the first query whose structured result contains every
requested fact. The successful fact-yielding query is not counted.
