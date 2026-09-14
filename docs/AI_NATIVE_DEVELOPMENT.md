# AI-native Moss development

Moss exposes compiler knowledge through the versioned `moss-agent-1` JSON protocol. A
fresh agent can learn the repository workflow without reverse-engineering generated Rust.

## Recommended loop

```text
1. moss agent bootstrap --json
2. moss check <source> --json
3. inspect / why / effects / ownership / cost as needed
4. edit Moss source (or use an exact semantic edit)
5. moss fmt
6. moss impact <target> --json
7. moss test --affected --json
8. moss test when a full validation run is appropriate
```

Inside a project, `moss build`, `moss test`, and `moss bench` reuse the same manifest,
compiler facts, durable project IDs, artifact cache, and JSON envelope. `moss test
--affected` is a development accelerator; the full suite remains authoritative.

Semantic queries and edits use the same temporary uber-module context as the target
containing `--source`: `src` queries include all application files, test queries
include `src + tests`, and benchmark queries include `src + benches`. A source outside
a project is analyzed as one standalone file. Physical paths remain part of every
result, so agents should pass `--source` when a line or local construct needs
disambiguation. Rename scope follows that context and expression edits are validated
transactionally against every participating file.

## Queries and identity

Use `inspect`, `type`, `effects`, `ownership`, `calls`, `awaits`, `why`, and `cost` to
ask the compiler what it already knows. `moss impact` compares the current checked
semantic snapshot with the previous successful check/build/test snapshot. It reports
implementation-only versus semantic-interface changes, dependency paths, affected
specializations/domains/tests/benchmarks, and reuse counters.

`entity-v1` identities are durable semantic edit/incremental identities. Phase 5 debug
maps still contain build/source-layout provenance identities for Moss-to-Rust/DWARF and
disassembly navigation; tooling must not treat those line-derived IDs as edit-stable.

## Repairs and uncertainty

Structured diagnostics use stable codes and always expose `fixes` and
`legal_alternatives`. The compiler supplies a fix only when the mechanical action is
unique. When intent matters—especially ownership—the alternatives are constraints for
the agent to choose from, not an automatic repair.

## Session self-report

At the end of a real agent session, record:

```text
Which Moss agent/compiler features did you use?
Which materially reduced iterations or ambiguity?
How many significant edit -> check -> repair cycles occurred?
Where did you still have to guess?
Did impact/affected testing avoid unnecessary work?
Which compiler-agent improvement would have saved the most time?
```

The same prompt is available with `moss agent session-report-template --json`.

## Reference implementation session

During the Phase 6 implementation, the agent used `bootstrap`, structured `check`,
semantic `effects`/`ownership`/`why`/`cost` queries, `impact`, canonical `fmt`,
exact `edit rename`, and `test --affected`. The main loop took several significant
edit -> check -> repair cycles; the structured diagnostics exposed the malformed
block-colon repair directly, while `impact` and affected testing avoided rerunning
the unrelated external test after a body-only change. No generated Rust had to be
reverse-engineered. The remaining ambiguity is intent-dependent ownership repair:
the compiler reports legal alternatives but deliberately does not choose one. The
highest-value next improvement would be broader exact semantic edit coverage for
more Moss constructs, not another parallel analysis.

The current implementation intentionally remains a deterministic CLI. It does not add
MCP, a network service, telemetry, or an automatic replacement for full validation. A
persistent compiler process is deferred until startup/analysis timings show that it
materially improves the edit loop.
