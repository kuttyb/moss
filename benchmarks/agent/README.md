# Moss agent benchmark

Phase 22.2 establishes a reproducible baseline for how well a fresh coding agent
can learn, write, repair, debug, and navigate Moss. The suite is tooling only: it
does not change the language, compiler acceptance, lowering, synchronization, or
interpreter behavior. This corpus intentionally measures the pre-Phase-22.1 agent
experience.

The complete, hardened corpus contains 30 small tasks under `tasks/`. It has ten
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
stable error codes. Narrow declarative file assertions and existing Moss semantic
queries verify each task's defining capability in addition to its output. They do
not require equality with the reference solution.

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

Fresh agents used for Phase 22.2C must never have access to any benchmark
`expected/` reference tree. Baseline orchestration must stage a sanitized agent
workspace containing only the selected task's starter files and prompt, plus the
normal Moss repository tooling and documentation intentionally made available to
the agent. The repository's adjacent `expected/` directories are for corpus
maintenance and validator self-checks; they must remain outside the agent-visible
workspace.

Phase 22.2C uses `baseline.py` to stage and run the canonical baseline. Its fixed
generic prompt is `prompt-wrapper.txt`; the task-specific paragraph is copied
verbatim from `task.json`. Each task runs through a new ephemeral Codex process in
its own mount namespace. The namespace replaces the repository path with a
Git-free sanitized tree containing the starter under `task/`, the checked Moss and
Margo tools, repository agent skills, and normal documentation. An explicitly
empty `.git` mount prevents recovery through Git. Browser, plugin, memory, and
multi-agent features are disabled, and generated shell commands use the
network-disabled workspace sandbox. The orchestrator supplies the host's resolved
`rustc` path explicitly because the sanitized home intentionally contains no
`rustup` configuration; this keeps Margo's native workflow available without
exposing user state.

Before a canonical run, verify staging and namespace behavior:

```sh
python3 benchmarks/agent/baseline.py sanity
python3 tests/tooling/check_agent_baseline.py ./moss
```

The canonical command records the frozen corpus/compiler commits and refuses to
overwrite existing artifacts:

```sh
python3 benchmarks/agent/baseline.py run-all \
  --corpus-commit <frozen-corpus-commit> \
  --compiler-commit <compiler-commit>
```

The protocol defines a meaningful validation attempt as a direct `moss
check|build|test|debug`, `moss run --interp`, or `margo build|test|run`
invocation. Bootstrap and semantic queries are informational and do not increment
attempts-to-green. The wrapper logs direct Moss/Margo commands, exit status,
duration, and structured diagnostic codes without changing their output. Final
pass/fail always comes from the benchmark validator outside the agent namespace.
Canonical artifacts retain each prompt, final task tree, tool log, validator
result, and compact manifest; agent event transcripts and generated build products
remain disposable runtime data.

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

Phase 22.2A's benchmark corpus is complete, hardened, and frozen for the baseline.
Phase 22.2B's runner and result schema are complete. Phase 22.2C's fresh-agent
baseline run and Phase 22.2D's aggregate report remain pending.

Benchmark tasks must not be changed merely because later compiler diagnostics improve. The same task corpus should be reusable to measure whether agent-facing improvements actually help.

Tasks may be corrected when they are objectively invalid or rely on unsupported
Moss behavior. Such corrections must preserve stable IDs and document the corpus
revision in `CORPUS_REVISIONS.md` so baseline comparisons can account for it.
