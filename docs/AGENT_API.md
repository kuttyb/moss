# Moss Agent Semantic API

Phase 6A exposes compiler facts through one deterministic, vendor-independent JSON
protocol. It is a read-only interface over the analyses already used for Moss type,
ownership, effect, await, functional, and backend decisions. It is not an editing API,
daemon, MCP server, or replacement compiler pipeline.

## Bootstrap and discovery

Start an automated Moss session with:

```sh
moss agent bootstrap --json
```

The result contains protocol and compiler versions, the nearest project root when a
`moss.toml` is present, capabilities, available queries, workflow guidance, and basic
safety rules. Schema and capability discovery are also available:

```sh
moss agent capabilities --json
moss agent schema --json
```

All agent commands use this top-level envelope:

```json
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.2.0",
  "command": "effects",
  "ok": true,
  "result": {}
}
```

Failures replace `result` with a structured `error`. Protocol version 1 keeps a common
diagnostic shape: code, severity, message, source file, line/column/span, source
provenance identity and symbol where available, plus a fixed `details` object for richer
ownership/cycle facts.

## Structured diagnostics

```sh
moss check src/main.moss --json
```

Successful checks return a diagnostics array, including warnings such as
`MESSAGE_PAYLOAD_COPY_LARGE`. Compile failures return stable category codes such as
`OWNERSHIP_USE_AFTER_CONSUME`, `AWAIT_CYCLE`, `RECURSION_CYCLE`,
`AWAIT_TARGET_UNBOUNDED`, and `TYPE_INFERENCE_FAILED`. Human diagnostics remain the
default for `moss --check source.moss` and `moss check source.moss`.

## Semantic queries

Queries accept a semantic identity, construct name, or exact source line. The source is
provided explicitly:

```sh
moss inspect fn:evaluate --source src/main.moss --json
moss type line:18 --source src/main.moss --json
moss effects fn:normalize --source src/main.moss --json
moss ownership method:Buffer.push --source src/main.moss --json
moss calls fn:evaluate --source src/main.moss --json
moss awaits handler:Router.Route --source src/main.moss --json
moss why main@24:expression:0 --source src/main.moss --json -O
```

An exact location can alternatively be written as one target:

```sh
moss inspect src/main.moss:18 --json
```

`inspect` combines type, ownership, observable effects, static calls, await facts, and
existing compiler explanations. The focused commands return the same authoritative
subsets. Ownership (`READ`, `WRITE`, `CONSUME`) remains separate from observable effects
such as local mutation, domain access, message, await, I/O, failure, and divergence.

`calls` reports only statically resolved targets retained by recursion validation.
`awaits` reports validated await sites and the global domain dependency edges, including
their Moss source lines. `why` reuses existing functional materialization/fusion/semantic
rewrite notes and backend lowering decisions; it does not reconstruct a separate
optimization analysis.

Selectors must resolve exactly. A missing target returns `QUERY_TARGET_NOT_FOUND`; Moss
does not guess a nearby semantic entity or invent a dynamic target.

## Project, test, and benchmark results

Phase 7 reuses the same protocol rather than defining a second automation format:

```sh
moss build --json
moss build --release --json
moss test parser --json
moss bench normalize --json
```

Build results identify the profile and deterministic artifact paths. Test and benchmark
records include stable project/source identities. Test assertions use
`TEST_ASSERTION_FAILED`; project and benchmark failures use the stable categories
documented in the build/testing/benchmark guides. Benchmark baseline compatibility is a
structured warning. These commands share the normal compiler pipeline and semantic
identities; they do not introduce affected-test analysis or performance facts into the
semantic query layer.

## Minimal AGENTS.md onboarding

Repositories may use this intentionally short snippet:

```text
This repository uses Moss.

Before modifying Moss source, run:

    moss agent bootstrap --json

Use Moss structured diagnostics and semantic queries rather than
reverse-engineering or editing generated Rust.
```

## Deliberately deferred

Phase 6A does not add semantic edits or repairs, cross-edit durable semantic hashing,
deep impact analysis, incremental dependency cones, affected-test selection, performance
facts, MCP, a daemon/server, an AI benchmark harness, or a substantial formatter. Phase
5 source/provenance identities remain deterministic for one source layout; stronger
durable identities across edits remain future work.
