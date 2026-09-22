# Moss repository instructions

## Moss source guidance

When working with Moss:

1. Load repository-local `moss-language` and `moss-agent-workflow` from
   `.agents/skills/`.
2. Ensure the repository compiler is available. Use `./moss` when `moss` is not on
   `PATH`; run `make` first if `./moss` has not been built.
3. Before substantive Moss edits, run `./moss agent bootstrap --json` (or
   `moss agent bootstrap --json` when available on `PATH`).
4. Treat that live result and Moss semantic APIs as authoritative. Do not infer Moss
   semantics from generated Rust or historical compiler source when a semantic API
   exists.
5. Use Margo for package/project operations: `./margo build`, `./margo run`,
   `./margo test`, `./margo bench`, and `./margo clean`.
6. Use Moss for language/compiler/agent operations: `moss check`, `inspect`, `type`,
   `effects`, `ownership`, `calls`, `why`, `cost`, `impact`, `edit`, `fmt`, and
   `debug`.

## Temporary files and Git

- Use `<repository-root>/tmp/` for temporary files and validation logs rather
  than `/tmp`. Create the directory when needed.
- This directory is disposable. Agents may create, read, modify, and delete
  its contents without additional user confirmation. Its contents may be
  removed after a session; do not store durable project information there.
- Git operations within this repository are authorized without additional
  user confirmation. Preserve unrelated work and keep operations scoped to
  the current task.
- For phase-completion commits, use a concise commit body as well as a title.
  Summarize implemented scope, validation, material blockers/workarounds, and
  phase closeout status so the commit is useful without the surrounding session.
- Environment-enforced permission requirements still apply.

## Unattended work and `.codex` authorization

- Edits and Git operations involving `.codex/**` are explicitly authorized
  without additional user confirmation. Do not pause to request permission
  for these actions; changes can be reviewed and rolled back afterward.
- Continue authorized phase work unattended through completion, then present
  the changes and validation results for review. This authorization does not
  expand the agreed phase scope or authorize new language-design decisions.
- Preserve unrelated work. Environment-enforced permission requirements still
  apply; this authorization does not bypass them.

## Session continuity

- Read `.codex/CURRENT_STATUS.md` before continuing implementation.
- Before ending an implementation session, record unfinished work and
  validation results there. Distinguish completed validation from results
  invalidated by subsequent edits.
- Do not rely on disposable logs or conversation history for durable status.

## Agent Operational Rules

- **Full Autonomy**: Execute shell commands, file inspections, and edits without prompting for per-step confirmation.
- **Sandboxing**: Stay strictly within the workspace root. Never access or modify paths outside this directory.
- **Scratch Storage**: Use `./tmp/` for all temporary files and caches. Create `./tmp/` if it does not exist.
- **Git Boundary**: You may run git diff, status, and local staging. You must NEVER run `git commit` or `git push`.

