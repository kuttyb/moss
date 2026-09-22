---
name: moss-agent-workflow
description: "Work efficiently on a Moss repository using the compiler-first moss-agent-1 workflow: bootstrap, structured diagnostics, semantic queries, formatting, impact, tests, Fast Debug, and traces. Use for Moss compiler errors, debugging, or agent-driven Moss edits."
metadata:
  language: moss-0.1
  skill-version: "1"
  validation: tests/tooling/check_agent_skills.py
---

# Moss agent workflow — current v0.1

## Fresh Agent Bootstrap

This is the required start of every fresh Moss task:

1. Read repository `AGENTS.md`.
2. Ensure `./moss` exists; run `make` if it does not.
3. Load `moss-language`.
4. Load `moss-agent-workflow`.
5. Run `./moss agent bootstrap --json`.
6. Read its `tool_invocation`, language/compiler/protocol versions, capabilities, semantic queries,
   actions, debugging features, safety rules, language constraints, and recommended
   workflow.
7. If an unfamiliar capability is reported, run `./moss agent capabilities --json`
   and/or `./moss agent schema --json` for its purpose, inputs, response shape, stable
   identities, and common failure modes.
8. Use `./margo` for package/project work and `./moss` for language and semantic work.
9. Only then inspect or edit Moss source.

In a repository checkout, prefer `./moss` and `./margo`. Bare `moss` and
`margo` are valid when those executables are already available on `PATH`; the
bootstrap contract treats them as logical tool names, not different tools.

The language skill declares `moss-0.1`. Compare it with bootstrap's
`result.language_version`; a mismatch means the skill may be stale. Refresh live
discovery and report the mismatch rather than trusting historical guidance.

Use the compiler as the authority on Moss semantics. This skill is a compact operating
procedure for an agent working on `.moss` source; it complements `moss-language`,
which explains the current language surface. Do not infer Moss behavior from generated
Rust or historical fixtures when a checked compiler fact is available.

> Do not learn Moss through trial-and-error compilation when the compiler can answer
> the question directly. Do not infer Moss semantics from generated Rust when a
> semantic API exists.

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
  fresh_agent_bootstrap: required
  capability_discovery: agent-capabilities-and-schema
  synchronization_introspection: inspect-effects-why
  module_interface_truth: mossi-semantic-interface
  structured_json: preferred
  project_driver: margo
  project_manifest: Moss.toml
  project_lockfile: Moss.lock
  package_resolution: path-git
  module_resolution: moss-compiler
  semantic_oracle: moss-agent-1
  source_surface: bootstrap-discoverable
  canonical_docs: bootstrap-discoverable
  project_surface: bootstrap-discoverable
  collection_operations: bootstrap-discoverable
  test_domain_topology: bootstrap-discoverable
  composition_initializers: bootstrap-discoverable
  gap_classification: minimal-reproducer-first
```

## Route unfamiliar questions through discovery

For unfamiliar source syntax, collection APIs, tests, domains, or composition:

```text
bootstrap/source_surface
        ↓
canonical_docs.practical_language_guide when more context is needed
        ↓
structured diagnostic or semantic query
        ↓
minimal moss check --json probe
        ↓
classify
```

For project-layout or manifest questions, use:

```text
bootstrap/project_surface
        ↓
canonical_docs.project_workflow when more detail is needed
```

Do not filesystem-search arbitrary examples before these discovery surfaces.
The Gentle Introduction is the primary practical guide for fresh Moss-writing
work, not mandatory full reading for compiler-internals or narrowly targeted
tasks.

## Before declaring a language/compiler gap

Use this flow before creating a SWARM finding or turning a workaround into an
architectural recommendation:

```text
attempted form fails
        ↓
discover canonical Moss syntax (bootstrap source_surface / language skill)
        ↓
read the structured diagnostic
        ↓
use a semantic query where applicable
        ↓
construct the smallest reproducer
        ↓
native verification if backend lowering is implicated
        ↓
classify
```

Classify the result precisely: supported under different Moss syntax,
ergonomic/syntax-sugar omission, standard collection/API gap, intended ownership
rule, intended domain rule, compiler frontend/type-checking bug, compiler
native-lowering bug, Fast Debug coverage gap, agent misunderstanding, or genuine
language-design gap. Only distinct real issues receive SWARM IDs. Never infer a
language-design conclusion from generated Rust: valid Moss may expose a backend
lowering bug. Never turn a workaround into an “idiomatic Moss architecture” claim
until a minimal reproducer establishes the underlying limitation.

For unfamiliar operators, collection APIs, test/domain topology, or composition
initializers, prefer:

```text
bootstrap/source_surface
        ↓
canonical_docs.practical_language_guide / moss-language
        ↓
structured diagnostic or semantic query
        ↓
minimal moss check --json probe
        ↓
classify
```

For a project-layout question, inspect `project_surface` before reading
`canonical_docs.project_workflow`. Do not search arbitrary examples to discover
basic collection operations, the minimal `Moss.toml`, test topology, or the
composition-prefix initializer rule.

An absent guessed spelling is not proof that a construct is absent. Compiler
internals are for repairing an established compiler issue, not the first
language-discovery surface for a Moss programmer or fresh agent.

## Start a Moss task

1. Read repository `AGENTS.md`, load both Moss skills, and complete **Fresh Agent
   Bootstrap** above.
2. Use the versioned `moss-agent-1` bootstrap result as the live capability manifest;
   use `capabilities` and `schema` for progressive detail rather than compiler-source
   archaeology.
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

### Question → capability

| Question | Use |
| --- | --- |
| What is this symbol, construct, route, or concrete instance? | `inspect` |
| What type or specialization did it resolve to? | `type` |
| What does it READ / WRITE / CONSUME, and what observable effects occur? | `effects` |
| What capability does this parameter or call require? | `ownership` |
| What direct callers/callees are statically known? | `calls` |
| Why did a semantic, fusion, backend, or synchronization choice occur? | `why` |
| What static work/copy/materialization/backend facts are known? | `cost` |
| What could this edit invalidate or which tests could it affect? | `impact` then `test --affected` |
| What topology, classes, ranks, modes, or conflict witnesses are derived? | `inspect`, `effects`, or `why` → `synchronization_plan` |
| What happened during checked execution? | Fast Debug with `--trace` |

Use `moss agent schema --json` when a route's inputs or result shape are unclear.

### Synchronization and module truth

Never reconstruct Moss synchronization by reading generated Rust if the compiler can
expose the synchronization plan directly. `inspect`, `effects`, and `why` include
`synchronization_plan` and `synchronization_dump`. The structured plan contains the
concrete domain instance and specialization identity, `domain_rank`, handler R/W/C,
X*, `ProtectedRead`, `LockSet`, class membership/`ClassSet`, SHARED/EXCLUSIVE mode,
`class_rank`, and conflict matrix/witness facts. These are compiler-owned plan facts;
they are the first place to ask why handlers conflict.

For modules and source-free providers, **Moss source plus the `.mossi` semantic
interface is the language/module truth**. Run `moss build --json` to discover emitted
`result.artifacts.module_interfaces`; inspect that semantic interface and its reported
exports/effects/specializations before generated Rust. There is no separate source-free
Fast Debug fallback: it requires reachable Moss source.

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

Current verified limits include `for` traversal. Supported built-in collection
operations reached through object fields and the current eager functional pipeline
surface execute in Fast Debug; use native validation as well when checking parity.

With `--trace`, Fast Debug writes newline-delimited JSON events to standard error.
Use a bounded trace to diagnose logical behavior: reproduce, inspect the relevant
function/handler entry and exit, local/state access, branch, return, assertion, and
message/reply events, patch, then replay. Existing traces carry source and semantic
identity; they are not a physical lock, contention, or schedule simulator. Do not claim
trace slicing/query features that the compiler has not exposed.

## Packages/projects versus semantic/compiler operations

Use **Margo** for package/project orchestration:

```sh
margo build
margo run
margo test
margo bench
margo clean
```

`Moss.toml` is the package manifest; `Moss.lock` freezes resolved Git commits.
Dependencies may be local package roots (`{ path = "../geometry" }`) or Git sources
with `rev`, `tag`, or `branch`. Margo resolves that package graph and supplies artifact
roots. Moss still owns module declarations/imports, `.mossi` semantic interfaces,
checking, specialization, and lowering. Existing `moss build`, `moss test`, `moss
bench`, and `moss clean` are compatibility project paths, not the canonical package
workflow.

Use **Moss** directly for semantic/compiler work:

```sh
moss check path/to/file.moss --json
moss inspect|type|effects|ownership|calls|why|cost <target> --source <source> --json
moss impact <target> --source <source> --json
moss edit rename|replace-expression|change-argument ... --json
moss fmt
moss test --affected --json
moss debug <project-or-source> --trace
```

Generated Rust is useful for compiler/backend work and native debugging, but it is an
implementation artifact for Moss application changes. Prefer Moss diagnostics,
`mossmap` provenance, and semantic queries first. Use `margo test` for full root-package
verification after reduced Moss `test --affected` iteration.

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
