# Moss repository instructions

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

## Session continuity

- Read `.codex/CURRENT_STATUS.md` before continuing implementation.
- Before ending an implementation session, record unfinished work and
  validation results there. Distinguish completed validation from results
  invalidated by subsequent edits.
- Do not rely on disposable logs or conversation history for durable status.
