---
name: moss-agent-workflow
description: Work efficiently on a Moss repository using the compiler-first moss-agent-1 workflow: bootstrap, structured diagnostics, semantic queries, formatting, impact, tests, Fast Debug, and traces. Use for Moss compiler errors, debugging, or agent-driven Moss edits.
metadata:
  language: moss-0.1
  skill-version: "1"
  validation: tests/tooling/check_agent_skills.py
---

# Moss agent workflow — current v0.1

Use the compiler as the authority on Moss semantics. This skill is a compact operating
procedure for an agent working on `.moss` source; it complements `moss-language`,
which explains the current language surface. Do not infer Moss behavior from generated
Rust or historical fixtures when a checked compiler fact is available.

## Machine-checkable workflow contract

```yaml
moss_workflow_contract:
  language_version: moss-0.1
  skill_version: 1
  bootstrap: moss-agent-1
  diagnostics: structured-json
  source_of_truth: compiler
  generated_rust: implementation-artifact
  fast_debug: checked-moss-interpreter
  trace: newline-delimited-json
```

## Start a Moss task

1. Read repository `AGENTS.md` and load `moss-language` for any Moss source work.
2. Run `moss agent bootstrap --json` near the start of a new task. It returns the
   versioned `moss-agent-1` envelope, available capabilities, safety rules, and the
   current recommended workflow.
3. Inspect the relevant source and make the smallest edit that expresses the intended
   Moss change. Do not edit generated Rust.
4. Check the program first. For a standalone source, use
   `moss check path/to/file.moss --json`. In a project, use the source path that
   identifies the physical file you are investigating.
5. Read structured diagnostics before guessing at a repair. Diagnostics include a
   stable code, source span, identities where known, and only high-confidence
   mechanical fixes. `legal_alternatives` describes choices whose intent remains yours.

## Ask the compiler semantic questions

Use the existing JSON queries; do not reverse-engineer lowered Rust merely to learn a
type, call target, ownership mode, optimization barrier, route, or synchronization
fact:

```sh
moss inspect <target> --source <source> --json
moss type <target> --source <source> --json
moss effects <target> --source <source> --json
moss ownership <target> --source <source> --json
moss calls <target> --source <source> --json
moss why <target> --source <source> --json
moss cost <target> --source <source> --json
moss impact <target> --source <source> --json
```

`inspect main --source <source> --json` includes the checked concrete-domain graph.
Query output is the common `moss-agent-1` envelope, including protocol/schema/compiler
versions, `ok`, and either `result` or structured `error`. Target selectors must resolve
exactly; Moss does not guess a nearby source entity.

`entity-v1` IDs are durable semantic identities for exact edits and incremental work.
They are distinct from build/source-layout provenance in `.mossmap`; do not use a debug
map identifier as an edit-stable target. Semantic records retain physical source paths,
specialization identities where exact, resolved static caller/callee edges, inferred
effects, and project topology facts already known to the compiler.

When `--source` belongs to a project, queries use the same logical source context as
the target: `src` sees all application sources, a test source sees `src + tests`, and a
benchmark source sees `src + benches`. A source outside a project remains standalone.
The requested physical path still disambiguates a result.

## Edit, format, and verify

For normal source changes, edit Moss text, then run:

```sh
moss fmt
moss check path/to/file.moss --json
moss impact <target> --source path/to/file.moss --json
moss test --affected --json
```

`moss fmt` is canonical and idempotent. It uses two-space indentation and removes
formatting entropy. `moss test --affected` is a development accelerator derived from
the semantic impact snapshot; full `moss test` remains the authoritative validation.
If no safe narrow selection is available, expect a conservative larger selection.

For narrowly supported exact semantic changes, JSON-only semantic edits avoid ambiguous
line manipulation:

```sh
moss edit rename <entity-id> <new-name> --json
moss edit replace-expression <entity-id> <expression> --json
moss edit change-argument <call-id> <index> <expression> --json
```

These operations resolve exactly, reject stale or ambiguous targets, format changed
files, and recheck the complete relevant project context before writing. Renames from
an application source affect participating `src` references; a rename chosen from a
test or benchmark source uses that corresponding logical context. Do not substitute a
semantic edit for a change it does not explicitly support.

## Debug behavior, not just types

Fast Debug runs checked Moss directly rather than compiling Moss to Rust:

```sh
moss run --interp path/to/file.moss
moss run --interp --trace path/to/file.moss
moss debug <project-or-source>
moss debug <project-or-source> --trace
```

All reachable Moss source in that run uses the interpreter; it does not dynamically mix
some Moss functions with native Moss execution. A source-free provider remains a
production facility and needs source for Fast Debug. The interpreter has explicit
feature limits, so an unsupported construct reports a Moss-level limitation rather
than silently switching execution engines.

With `--trace`, Fast Debug writes newline-delimited JSON events to standard error.
Use a bounded trace to diagnose logical behavior: reproduce, inspect the relevant
function/handler entry and exit, local/state access, branch, return, assertion, and
message/reply events, patch, then replay. Existing traces carry source and semantic
identity; they are not a physical lock, contention, or schedule simulator. Do not claim
trace slicing/query features that the compiler has not exposed.

## Projects and backend artifacts

For normal project work, use `moss build`, `moss test`, and `moss bench`; they share the
manifest, checked program, semantic identities, profile handling, and backend cache.
Generated Rust is useful for compiler/backend work and native debugging, but it is an
implementation artifact for Moss application changes. Prefer Moss diagnostics,
`mossmap` provenance, and semantic queries first.

Use release builds for benchmark work. A baseline comparison records compiler, profile,
machine, and backend toolchain identity. Do not treat an incompatible baseline as a
performance verdict.

## Current diagnostic rewrites worth remembering

- Retired `await` → use a synchronous `message` result directly.
- Retired `spawn` → construct a domain in `main`'s composition prefix.
- Self-send or same-domain handler message → extract an ordinary helper.
- Domain handle in a payload/parameter/reply/collection → declare a static
  `domainroutes` dependency instead.
- Ownership conflict → inspect `ownership`/`effects`, then choose intent; do not
  invent implicit copies or source-level mode annotations.
- Pipeline/fusion surprise → use `why` and `effects`; an observable message or a
  callback that may fail/diverge is a real semantic boundary.

## Session closeout

At the end of a real Moss task, record a short self-report. `moss agent
session-report-template --json` provides the standard prompt. Note which compiler
features reduced ambiguity, significant edit/check/repair cycles, remaining guesses,
whether impact/affected tests avoided work, and the next most valuable compiler-agent
improvement. This is local dogfooding, not telemetry.

The skill is operational guidance, not a substitute for the compiler or a new language
specification. If it drifts, correct the skill/docs and retain current compiler
semantics. Broader measured agent evaluation, richer trace slicing, and advanced repair
work remain future Phase 22 work.
