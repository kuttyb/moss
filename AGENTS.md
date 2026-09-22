# Moss repository instructions

## Mandatory Session Startup

Every agent session must execute the following startup sequence before inspecting or editing Moss code:

Repository-local tools:

- compiler: `./moss`
- project driver: `./margo`

If `./moss` is missing, run `make` first. The first live discovery command is
`./moss agent bootstrap --json`. Bare `moss` or `margo` are valid only when
those executables are already available on `PATH`.

1. **Load Skills and Docs**: Load repository-local skills `$moss-language` and `$moss-agent-workflow` from `.agents/skills/`. For fresh Moss source-writing or unfamiliar language work, `docs/GENTLE_INTRODUCTION_TO_MOSS.md` is the primary practical guide; use the bootstrap's `canonical_docs` routing for formal, project, or testing detail as needed.
2. **Run Discovery**: Run `./moss agent bootstrap --json` (or `moss agent bootstrap --json` if on `PATH`). Run `make` first if `./moss` is not yet built.
3. **Verify Contract**: Ensure `result.language_version` matches `moss-0.1`.
4. **Tooling Split**:
   - Use `margo` for package/project operations: `./margo build`, `./margo run`, `./margo test`, `./margo bench`, and `./margo clean`.
   - Use `moss` for language, semantic queries, diagnostics, and formatting: `moss check`, `inspect`, `type`, `effects`, `ownership`, `calls`, `why`, `cost`, `impact`, `edit`, `fmt`, and `debug`.

## Before declaring a Moss gap

Before reporting a Moss language/compiler/API feature as missing:

1. Consult `moss-language`.
2. Run and read `moss agent bootstrap --json`; use capabilities/schema if needed.
3. Check current canonical docs/examples: consult `docs/GENTLE_INTRODUCTION_TO_MOSS.md` for syntax, collections, project layout, and domains, and `docs/MOSS_V0_1_LANGUAGE_DESIGN.md` for formal semantics.
4. Reduce uncertainty to the smallest Moss probe and run `moss check --json`.
5. If native lowering is implicated, verify with `margo build`, `margo test`, or `margo run`.
6. Classify the result before recording a SWARM finding.


Failure of one guessed spelling is not evidence that the underlying feature is
absent. An ownership diagnostic is not evidence for an architectural Moss idiom.
Do not infer a language-design conclusion from generated Rust: it may expose a
backend compiler bug even when Moss source semantics are valid.

### Key Language Rules Cheat Sheet

- **Structural Traits**: Traits are compile-time structural predicates (duck typed), never nominal declarations (no `implements Trait`). Specialization is static; no runtime trait objects, dynamic dispatch, or vtables.
- **Inferred Effects**: Parameter effects (`READ`, `WRITE`, `CONSUME`) are inferred by the compiler. Never write parameter mutation modifiers (`mut`, `inout`, `write`). Overlapping arguments are restricted to `READ + READ`.
- **Synchronous Communication**: Domains communicate strictly via synchronous, blocking `message target.Handler(args...)`. Value-returning handlers must terminate with `reply expr`. Self-send (`message self.X(...)`) and same-domain handler-to-handler messages are illegal (use ordinary helper functions).
- **Static Closed Routing**: Domains own mutable shared state and are constructed statically in `main`'s composition prefix. Outbound routes are declared with `domainroutes(...)` and bound at construction. Domain handles are static routing capabilities, not data values (cannot be passed as args/payloads/replies, or stored in collections/state).
- **Concurrency & Locks**: Programmers never write lock syntax. The compiler derives synchronization classes and `(domain_rank, class_rank)` lock ordering for safe, deadlock-free 2PL.
- **No Ordinary Recursion**: Ordinary recursion is unsupported in v0.1; express algorithms iteratively or via supported pipeline operations (`map`, `filter`, `reduce`).
- **Retired Syntax**: `await` and `spawn` are retired. Use synchronous `message` and static construction in `main`.

### Fast Debug Workflow

- **Deterministic Semantic Execution**: Run `moss run --interp <source>` or `moss debug <project-or-source>` to execute checked Moss code directly without rustc compilation.
- **Structured Traces**: Use `--trace` (`moss run --interp --trace <source>` or `moss debug <target> --trace`) to stream newline-delimited JSON events (function/handler entry/exit, state access, branch, message/reply, assertions) on stderr.
- **Query the Compiler**: Do not use interactive debuggers (e.g. LLDB) or guess semantics from generated Rust. Use semantic queries (`moss inspect`, `moss why`, `moss effects`, `moss ownership`, `moss calls`) to diagnose behavior and compiler decisions.

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
