# Agent benchmark corpus revisions

The Phase 22.2 corpus is frozen for baseline comparison. Any correction after the
freeze must identify an objective defect, preserve stable task IDs, and record
whether the agent-visible prompt or starter changed.

## Revision 1 — AB024 filename-independent module validation

- Previous corpus commit: `22fdb024e4f284d17fb9abfe887856e17127aae7`
- Affected task: `AB024`
- Prompt changed: no
- Starter changed: no
- Expected reference changed: no
- Compiler changed: no

The first provisional AB024 session produced a valid Margo project with modules
`Math` and `App`, exported and invoked `Math.answer`, and printed `42`. Margo's
build/run validations passed, but the benchmark rejected the solution because its
files were named `src/Math.moss` and `src/App.moss`. Neither the prompt nor the
success criteria required the reference solution's lowercase `src/math.moss` and
`src/main.moss` filenames.

The correction adds a generic filename-independent source-glob assertion and uses
it to locate the required module declarations and qualified call in any
`src/*.moss` files. Behavioral and Margo validations remain unchanged. A focused
regression proves the alternate filenames pass while the existing single-file
shortcut still fails. The original provisional AB024 evidence is retained as
non-canonical, and AB024 is rerun once in a new fresh context for the canonical
baseline after this revision.

## Revision 2 — AB020 pure-stage validation

- Previous corpus commit: `faee3cd46d15d08d046c3b93b43585df4406beba`
- Affected task: `AB020`
- Prompt changed: no
- Starter changed: no
- Expected reference changed: no
- Compiler changed: no

The first canonical-series AB020 session correctly removed the effectful captured
state and used the supported pure `filter(_ > 0)` stage followed by `sum`, which
compiled and preserved output `42`. The task prompt asks for a pure supported
stage and its success criteria do not require `map`, but the assertion accepted
only the reference solution's `map(...)` spelling.

The correction accepts either `map` or `filter` as the required pure functional
stage while retaining compilation, exact output, and `sum` checks. A focused
regression proves the alternate pure filter repair passes. The original AB020
session is retained as non-canonical, and AB020 is rerun once in a new fresh
context for the canonical baseline after this revision.
