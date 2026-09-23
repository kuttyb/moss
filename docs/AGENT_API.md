# Moss Agent Semantic API

## Closed concrete domain topology

`moss inspect main --source app.moss --json` includes `concrete_domain_graph`,
the same checked graph used by compilation. Each instance exposes
`concrete_instance_id` (also `identity`), `binding`, `source_domain_id`,
`specialization_id`, `domain_rank`, `source_file`, and `line`. Edges expose
`source_instance`, `route`, `target_instance`, `source_file`, and `line`.

Every instance links to its `domain-specialization:Domain:instance` record,
also queryable with `inspect`. Non-generic instances have concrete records too,
but reuse the source declaration's backend layout. Internally the graph holds
explicit references to the specialization record and source declaration, not
reconstructed Rust type names. Module qualification remains part of domain identity.

Handles cannot cross parameters, payloads, replies, or ordinary storage.
Consequently handler message targets are declared routes, while main targets
are concrete composition bindings. The graph's `closed` flag is set only after
all executable bodies pass the capability checks. Route-cycle diagnostics attach to the first
witness binding and include each edge's physical source location. Domain ranks
are whole-program ordinals; they are not synchronization classes or `.mossi` ABI.

Phase 6 exposes compiler facts and bounded development actions through one deterministic,
vendor-independent JSON protocol. It is a view over the analyses already used for Moss
type, ownership, effect, domain, functional, and backend decisions; edits remain exact and
validated. It is not a daemon, MCP server, or replacement compiler pipeline.

## Bootstrap and discovery

Start an automated Moss session with:

```sh
./moss agent bootstrap --json
```

The result contains protocol and compiler versions, the nearest project root when a
`moss.toml` is present, capabilities, available queries, workflow guidance, and basic
safety rules. Schema and capability discovery are also available:

```sh
./moss agent capabilities --json
./moss agent schema --json
```

In a repository checkout use `./moss`; bare `moss` is valid when the executable
is already on `PATH`.

All agent commands use this top-level envelope:

```json
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0",
  "command": "effects",
  "ok": true,
  "result": {}
}
```

Failures replace `result` with a structured `error`. Protocol version 1 keeps all
existing fields and additively exposes compiler-owned teaching facts where the
rejecting analysis has them: `source`, `rule`, `cause.entities`, `related`, and
`guidance`. Cause entities can identify a semantic kind/name/identity, source
expression, argument index, and inferred `READ`/`WRITE`/`CONSUME` access. Fields are
`null` or empty when the compiler has no sound fact; clients must not manufacture a
repair from missing data. The existing flat source fields and `details`, `fixes`, and
`legal_alternatives` remain compatible.

Within `cause.entities`, `semantic_identity` has one meaning: the compiler's actual
semantic identity for a resolved entity, or `null`. Canonical query selectors such as
`handler:Store.Read` remain in `name`; shortened selectors and durable `entity-v1`
identifiers are never placed in `semantic_identity`. The bootstrap
`diagnostic_contract.cause_entity_semantic_identity` and schema
`identity_contracts.diagnostic_cause_entity` fields expose this contract directly.

## Structured diagnostics

```sh
moss check src/main.moss --json
```

Successful checks return a diagnostics array, including warnings such as
`MESSAGE_PAYLOAD_COPY_LARGE` when the selected boundary physically materializes a
payload. Compile failures return stable category codes such as
`OWNERSHIP_USE_AFTER_CONSUME`, `RECURSION_CYCLE`, and
`TYPE_INFERENCE_FAILED`. Phase 22.1 also gives established domain and functional
rules specific IDs such as `DOMAIN_SELF_MESSAGE`,
`DOMAIN_HANDLER_REQUIRES_MESSAGE`, `DOMAIN_ROUTE_NOT_DECLARED`,
`FUNCTIONAL_PLACEHOLDER_REQUIRED`,
`FUNCTIONAL_CALLABLE_INVOCATION_UNSUPPORTED`, and
`FUNCTIONAL_CAPTURE_MUTATION`. `OWNERSHIP_CONFLICTING_ACCESS` and
`QUERY_TARGET_NOT_FOUND` retain their existing stable IDs while gaining structured
causes and guidance. Human diagnostics use the same facts and remain the default for
`moss --check source.moss` and `moss check source.moss`.

`moss agent bootstrap --json` advertises the compiler-owned vocabulary in
`diagnostic_contract.guidance_kinds`. Clients should treat these values as stable
repair categories and use the accompanying summary for the concrete legal action;
they must not infer unadvertised repairs from message text.

Each command schema labels `common_failure_modes` as `representative, not exhaustive`.
The `check` schema includes every stable Phase 22.1 diagnostic family, but clients
must continue to handle other stable compiler errors rather than treating that list
as a closed enum.

For example, an overlapping call exposes both actual argument roles:

```json
{
  "code": "OWNERSHIP_CONFLICTING_ACCESS",
  "rule": {"id": "ownership.overlapping-access"},
  "cause": {
    "kind": "overlapping-actual-arguments",
    "entities": [
      {"kind": "argument", "argument_index": 1, "expression": "item", "access": "WRITE"},
      {"kind": "argument", "argument_index": 2, "expression": "item", "access": "READ"}
    ]
  },
  "guidance": {"kind": "separate-conflicting-access"}
}
```

## Semantic queries

Queries accept a semantic identity, construct name, or exact source line. The source is
provided explicitly:

```sh
moss inspect fn:evaluate --source src/main.moss --json
moss type line:18 --source src/main.moss --json
moss effects fn:normalize --source src/main.moss --json
moss ownership method:Buffer.push --source src/main.moss --json
moss calls fn:evaluate --source src/main.moss --json
moss why main@24:expression:0 --source src/main.moss --json -O
```

An exact location can alternatively be written as one target:

```sh
moss inspect src/main.moss:18 --json
```

`inspect` combines type, ownership, observable effects, static calls, concrete topology, and
existing compiler explanations. The focused commands return the same authoritative
subsets. Ownership (`READ`, `WRITE`, `CONSUME`) remains separate from observable effects
such as local mutation, domain access, synchronous message, I/O, failure, and divergence.

Effect precision follows the selected target. Callable targets expose their transitive
callable summary, and functional pipeline/node targets expose the precise summaries
already retained by functional analysis. An ordinary statement or binding has
`observable_effects: null` when Moss has no exact statement-level summary; it never
inherits unrelated effects from the rest of its function. In that case,
`enclosing_callable_effects` provides the separately labelled callable context when one
exists. Phase 6A does not run a new statement-effect analysis to answer a query.

`calls` reports only statically resolved targets retained by recursion validation.
The retired `awaits` query and await-only schema fields are removed. Concrete
routing remains available through `inspect` and its `concrete_domain_graph`.
`why` reuses existing functional materialization/fusion/semantic
rewrite notes and backend lowering decisions; it does not reconstruct a separate
optimization analysis.

Target JSON also includes `source_identity` (the semantic identity used for the current
build) and `specialization_identity` when the selected target is an actual specialization
record. Direct call records include `resolved: true`, `target_kind`, physical source
location, argument types, and a specialization identity only when the compiler has an
exact matching specialization. `inspect` and `calls` additionally expose `callers`
from the same retained static call graph. No field is inferred from generated symbol
spelling.

The Phase 10.5 synchronization schema reservation is superseded by Phase 10.6C.
`synchronization_diagnostics.availability` is now `derived`: graph-relative
classes, handler footprints, conflict witnesses, and metrics come from the stored
`SynchronizationPlan`. These are analysis facts, not a claim that production
class locks or handler-level 2PL have been emitted.

Selectors must resolve exactly. A missing target returns `QUERY_TARGET_NOT_FOUND`.
When a bare name exactly matches the final component of compiler-known targets, the
error reports every deterministic canonical candidate (for example,
`handler:Store.Read`) and `qualify-query-target` guidance. This is symbol-table
resolution, not fuzzy search; unrelated missing names receive no guessed candidate.

## Project, test, and benchmark results

Phase 7 reuses the same protocol rather than defining a second automation format:

```sh
moss build --json
moss build --release --json
moss test parser --json
moss bench normalize --json
```

Build results identify the profile, deterministic artifact paths, and the resolved Rust
backend toolchain fingerprint used for native cache validation. Test and benchmark
records include stable project/source identities. Test assertions use
`TEST_ASSERTION_FAILED`; project and benchmark failures use the stable categories
documented in the build/testing/benchmark guides. Benchmark baseline compatibility,
including the Rust backend identity, is a structured warning. These commands share the
normal compiler pipeline and semantic identities. Incremental work is available through
`moss impact <target> --json`; during iteration, `moss test --affected --json` selects
tests whose semantic dependency cone reaches a changed unit and reports skipped tests
with reasons. A missing snapshot is handled conservatively by selecting all tests.

### Project semantic context

When `--source` names a file inside a Moss project, every semantic query and edit
uses the same temporary uber-module as the corresponding project target. A source
under `src/` sees all `src/**/*.moss`; a source under `tests/` sees `src` plus
`tests`; and a source under `benches/` sees `src` plus `benches`. The requested
physical path still disambiguates the result and is returned in `source.file`.
Files outside a project retain standalone single-file behavior. There is no implicit
module or directory namespace.

## Durable identities, hashes, and cost facts

Query results include an `entity-v1` durable semantic identity, an implementation hash,
and a semantic-interface hash. These are distinct from Phase 5 `.mossmap` debug/source
identities, which are scoped to one source layout and build. An implementation-only body
change can stop compiler invalidation at the unchanged interface; verification still
selects dependent tests. Snapshots live below `.moss/semantic-cache-v1/` and are an
accelerator, not source-of-truth input.

`moss cost <target> --source file.moss --json` exposes factual costs already known to
the compiler: functional materialization/traversals, semantic work eliminated, static
message-materialization sizes, specialization counts, and selected domain backend/lock facts. It
does not predict runtime performance.

## Canonical formatting and semantic edits

`moss fmt` formats all project Moss files with two-space indentation and canonical
spacing. `moss fmt --check --json` reports whether a write would be needed. Formatting
validates source before and after rewriting and is idempotent.

Exact semantic edits are available in JSON mode:

```sh
moss edit rename entity-v1:function:normalize normalize_value --json
moss edit replace-expression entity-v1:binding:fn:main:total 'sum(values)' --json
moss edit change-argument entity-v1:call:fn:main:add 0 new_value --json
```

The initial implementation supports exact function renames, binding expression
replacement, and statically resolved call-argument replacement. Stale, ambiguous, or
unsupported targets are structured errors; edits are formatted and rechecked before
they are written. It never edits generated Rust. Diagnostics always carry `fixes` and
`legal_alternatives` arrays. A fix is present only for a high-confidence mechanical
action; alternatives describe choices without silently selecting program intent.

Renames are transactional. In an application (`src`) context they update the
definition and statically resolved references in all participating `src` files. A
rename selected from a test file uses `src + tests`, and one selected from a benchmark
file uses `src + benches`; only files in that logical context are rewritten. Expression
and argument edits change one physical file, then recheck the complete context before
committing any file, so a cross-file type error leaves the project untouched.

## Minimal AGENTS.md onboarding

Repositories may use this intentionally short snippet:

```text
This repository uses Moss.

If ./moss has not been built, run:

    make

Before modifying Moss source, run:

    ./moss agent bootstrap --json

Use ./moss for language/semantic operations and ./margo for project operations.
Bare moss/margo are valid when already on PATH.

Use Moss structured diagnostics and semantic queries rather than
reverse-engineering or editing generated Rust.
```

## Deliberately deferred

Phase 6 intentionally keeps the protocol CLI-based. It does not add MCP, a daemon/server,
telemetry, package management, or new language syntax. Phase 6 durable IDs are
deterministic and reasonably stable for ordinary edits, but remain versioned separately
from any stronger cross-edit identity model that may be needed in a future release.
## Module-aware results

Project builds expose emitted `.mossi` paths in `artifacts.module_interfaces`.
Semantic records use module-qualified declaration identities, distinguish
concrete and generic exports, and report exact concrete domain and specialization
identities. Impact snapshots retain concrete route dependencies. `moss impact` continues to use compiler-owned
semantic dependencies and invalidates generic users when their semantic body
hash changes.

## Derived synchronization diagnostics (Phases 10.6C/F)

`inspect`, `effects`, and `why` include `synchronization_plan` and
`synchronization_dump`. Both project the checked program's authoritative plan,
including exact instance/specialization identities, X*, leaf/class mappings,
handler R/W/C/X/ProtectedRead/LockSet/ClassSet, normalized effects, acquisition
modes, deterministic ranks, and conflict witnesses. `LockSet` contains leaves;
`ClassSet` contains class identities in local rank order. The dump is a readable
string available without another CLI command. Production locking consumes this
plan (`physical_lowering: "handler_2pl"`); the dump also shows ranked acquisitions
and full-handler retention.
See [the plan schema and derivation](SYNCHRONIZATION_PLAN.md).

## Fast Debug synchronous domains (10.6E)

Compile/check once, then interpret the checked transitive Moss source closure
with `moss run --interp --trace` or `moss debug`. Domains no longer require a
switch to native execution. Trace events identify composition, nested messages,
handler entry/exit, state READ/WRITE/CONSUME, and terminating replies with stable
instance/specialization/source identities. Fast Debug uses one deterministic
logical schedule; production lock events and contention remain separate runtime
validation concerns. See [Fast Debug](FAST_DEBUG.md).

The v0.1 diagnostic projection additionally exposes `handler_order`,
`conflict_matrix`, `class_opportunities`, and `class_splits`. Summary metrics include
state/protected leaves, classes, compression, handler acquisition counts, read-only
and self-conflicting handlers, and distinct-pair concurrency opportunities.
See [field meanings](SYNCHRONIZATION_PLAN.md#v01-diagnostic-projections-106f); these
are views of stored facts, not a second effect or synchronization analysis.
