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

## Completion recovery

`moss complete at:<line>:<column>` accepts an optional tooling overlay through
`--overlay-source`. The real physical source remains the location/project key.
Only the active line is recovered in memory: a partial type token receives a
known neutral type, an incomplete initializer receives a neutral value, and an
incomplete statement is elided to `pass` (or a neutral return/reply). Recovery
is bounded to that line, is reported as `result.recovery.used`, and is never
used by check, build, run, test, or normal parsing. It adds no Moss syntax and
does not make an invalid complete program valid.

## Phase numbering

The former Phase 22.5 reservation for repair/workflow automation is renumbered
to Phase 22.6. This phase implements only the Emacs Semantic IDE.

Installation and user workflow are documented in [TOOLING.md](TOOLING.md#emacs-installation).
