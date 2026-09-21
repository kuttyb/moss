# Moss repository instructions

## Moss source guidance

- When editing or generating `.moss` source, load the repository-local
  `moss-language` and `moss-agent-workflow` skills from `.agents/skills/`.
  They describe current Moss v0.1, not historical syntax. Use compiler diagnostics
  and `moss agent bootstrap --json` as the authority rather than inferring Moss from
  Rust or obsolete tests.

## Temporary files and Git

- Use `<repository-root>/tmp/` for temporary files and validation logs rather
  than `/tmp`. Create the directory when needed.
- This directory is disposable. Agents may create, read, modify, and delete
  its contents without additional user confirmation. Its contents may be
  removed after a session; do not store durable project information there.
- Git operations within this repository are authorized without additional
  user confirmation. Preserve unrelated work and keep operations scoped to
  the current task.
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
