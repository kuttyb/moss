# Moss agent benchmark

Phase 22.2 establishes a reproducible baseline for how well a fresh coding agent
can learn, write, repair, debug, and navigate Moss. The suite is tooling only: it
does not change the language, compiler acceptance, lowering, synchronization, or
interpreter behavior. This corpus intentionally measures the pre-Phase-22.1 agent
experience.

The fixed initial corpus contains 30 small tasks under `tasks/`. It has ten
write-from-scratch tasks, ten focused repairs, and ten workflow/debug tasks across
basic language, domains/messages, ownership/effects, functional dataflow,
synchronization, modules/Margo, Fast Debug, and tooling/navigation. Every task is
self-contained:

```text
tasks/AB001_simple_computation/
  task.json       stable prompt, paths, criteria, and validation argv
  starter/        files presented to a fresh agent
  expected/       reviewed reference solution used to validate the task itself
```

`task.json` follows `task.schema.json`. Validation commands are argument arrays,
not shell snippets. The runner accepts only the declared Moss and Margo command
families, checks metadata before execution, and reports duplicate IDs, absent
starter/reference directories, unsafe relative paths, and invalid commands with
stable error codes.

From the repository root:

```sh
./moss agent benchmark list
./moss agent benchmark list --json
./moss agent benchmark show AB001 --json
./moss agent benchmark validate --json
./moss agent benchmark run AB029 --json
./moss agent benchmark run AB001 --workdir ./tmp/AB001-work --json
```

`validate` checks all metadata and runs each task's validation against its reviewed
`expected/` tree. `run` validates `starter/` by default, or a prepared agent work
directory supplied with `--workdir`. External Phase 22.2C orchestration can copy a
task's starter directory, give the task prompt to a fresh agent, and then pass that
copy back to `run`. The core runner never invokes an AI model.

Each execution occurs in a fresh copy below `tmp/agent-benchmark/`; compiler or
Margo build output cannot modify the task corpus or the supplied work directory.
Before validation, the runner compares the prepared tree to `starter/`. It reports
all changed paths and fails when a change falls outside `allowed_paths`. A failed
task therefore cannot contaminate a later task.

Run results follow `result.schema.json` and use schema version
`moss-agent-benchmark-1`. The core runner fills final status, validation details,
compiler diagnostics, changed paths, and path violations. It reserves nullable
fields for agent identity, attempts, tool-call count, and wall-clock time so the
22.2C orchestrator can record those values without changing the result shape. The
future baseline will aggregate first-attempt success, iterations to green,
diagnostics encountered, tool-call count, final pass/fail, and changes outside the
allowed paths.

To add a task:

1. Allocate the next stable `ABNNN` ID and a descriptive directory/name.
2. Add a small deterministic `starter/` tree and one focused natural-language
   prompt. List every path the agent may change.
3. Express objective checks as declarative validation argv and optional file
   assertions. Do not use internet access or subjective review.
4. Add a complete `expected/` tree and run
   `./moss agent benchmark validate --json`.
5. Run `python3 tests/tooling/check_agent_benchmark.py ./moss`.

Benchmark tasks must not be changed merely because later compiler diagnostics improve. The same task corpus should be reusable to measure whether agent-facing improvements actually help.

Tasks may be corrected when they are objectively invalid or rely on unsupported
Moss behavior. Such corrections should preserve stable IDs and be documented so
baseline comparisons can account for the corpus revision.
