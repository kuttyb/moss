# Phase 22.5 — Emacs Semantic IDE

Phase 22.5 provides a stock-Emacs semantic IDE without an LSP server. The Moss
compiler remains the semantic authority; `moss-mode` is a client of the
`moss-agent-1` JSON commands.

## Architecture

```text
moss-mode xref / CAPF / call tree
                |
                `-- moss references / symbols / complete / calls --json
                              |
                              `-- checked entity-v1, call, type, module,
                                  scope, and domain-topology records
```

`references` does not grep source, `symbols` is not textual project search,
and completion does not keep an editor-side semantic index. Generated Rust is
not an input to any semantic editor feature.

The normal checker retains a minimal `SemanticUse` for each resolved local or
declared-type use: exact target identity, use kind, physical source and line,
and enclosing semantic identity. Callable references continue to project the
checker's `SemanticCallEdge` and functional callable records. Consequently,
`references` is projected from compiler-resolved semantic uses and does not
infer references from source text. Nested `Vector[T]`, `Map[K, V]`, and
`Queue[T]` type uses are resolved while checking and retained as identities;
the query does not compare or parse type spellings.

Receiver builtin completion and normal checking share the compiler-owned
`BuiltinOperationDescriptor` registry for Vector, Map, Queue, and String.
Functional pipeline completion shares `FunctionalOperationDescriptor` with
the parser/checker recognition of `map`, `filter`, `reduce`, `sum`, `count`,
`any`, and `all`. There is no completion-only API table.

## Unsaved editor overlays

`resolve`, `references`, `symbols`, `calls`, and `complete` accept
`--overlay-source <temporary-file>`. The `--source` path remains the physical
module/project identity and diagnostic path; the overlay supplies the current
buffer contents for that file. `moss-mode` uses this contract for definitions,
references, semantic search, callers/callees, call-tree expansion, and
completion. A valid unsaved buffer is therefore authoritative. An invalid
navigation overlay returns its structured parser/checker error and never falls
back to saved contents.

## Completion recovery

`moss complete at:<line>:<column>` accepts an optional tooling overlay through
`--overlay-source`. The real physical source remains the location/project key.
Only the active line is recovered in memory: a partial type token receives a
known neutral type, an incomplete initializer receives a neutral value, and an
incomplete statement is elided to `pass` (or a neutral return/reply). Recovery
is bounded to that line, is reported as `result.recovery.used`, and is never
used by check, build, run, test, or normal parsing. It adds no Moss syntax and
does not make an invalid complete program valid.

This bounded recovery is exclusive to `complete`. Overlays for `resolve`,
`references`, `symbols`, and `calls` are checked strictly.

## Informational latency measurement

Run:

```sh
python3 tests/tooling/measure_phase225_semantic_ide.py ./moss --samples 25
```

The utility measures one first invocation, excludes one warmup, then reports
the median and p95 of 25 subsequent fresh-process invocations. On the Phase
22.5 representative project on 2026-09-29, the observations were:

| Query | First | Median | p95 |
| --- | ---: | ---: | ---: |
| `complete` | 5.985 ms | 6.856 ms | 15.913 ms |
| `references` | 5.884 ms | 6.965 ms | 10.163 ms |
| `symbols` | 6.344 ms | 8.839 ms | 13.761 ms |
| `calls` | 15.191 ms | 7.006 ms | 11.508 ms |

These are informational observations, not thresholds. Every measured request
starts a compiler process, so process startup is part of—and at these sizes a
material share of—the latency. The Emacs unchanged-buffer cache avoids a new
process for an identical request within its cache window; any edit or save
invalidates it. This corrective pass adds neither a daemon nor an LSP server.

## Phase numbering

The former Phase 22.5 reservation for repair/workflow automation is renumbered
to Phase 22.6. This phase implements only the Emacs Semantic IDE.

Installation and user workflow are documented in [TOOLING.md](TOOLING.md#emacs-installation).
