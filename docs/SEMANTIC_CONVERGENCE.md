# Phase 10.5: Semantic convergence and AI introspection

This document records the current, implementation-backed semantic model and
the facts exposed to tools and agents. It is a convergence document, not a new
language specification. Phase 10.5 does not choose any of the still-open
ownership, synchronization, handle, ABI, or backend-linking designs.

## What is already implemented

The compiler already has one checked semantic pipeline shared by native builds,
the `moss-agent-1` JSON API, the `.mossmap` debug map, functional optimization,
and Fast Debug's checked-AST interpreter. In particular, the current tree
already provides:

- deterministic `entity-v1` semantic identities and physical source paths;
- typed functional IR with named functions, statically bound methods,
  placeholders, and specialized higher-order callables;
- ownership (`READ`, `WRITE`, `CONSUME`) and observable-effect summaries,
  including `may_fail` and conservative `may_diverge`;
- static call and await graphs, source-aware diagnostics, impact analysis, and
  cost facts;
- newline-delimited Fast Debug trace events for the settled ordinary-language
  execution facts.

The Phase 6 API extends these existing facts rather than performing an
AI-specific analysis. Query targets now retain a `source_identity`, explicit
`specialization_identity` where one is known, resolved call target kind, and a
caller list. A specialization identity is emitted only when the compiler has a
matching concrete specialization record; no type or target is inferred from a
name.

## First-order effect graph after specialization

Moss functional callables are closed statically. A named function, a statically
bound instance method, a placeholder expression, or a higher-order callable
parameter is resolved at its concrete call site before its effect summary is
used for optimization or dependency reporting. The resulting graph is
first-order:

```text
pipeline node -> concrete function/method specialization -> ordinary calls
```

There are no runtime callable objects, vtables, or indirect effect edges. A
generic higher-order body may be temporarily unresolved while its declaration
is checked, but a concrete functional pipeline cannot enter checked functional
IR with an unresolved callable. Debug builds assert this compiler invariant.

Captures participate in the same graph. Reading captured domain state is a
domain READ effect; mutating or communicating through a capture contributes the
corresponding observable effect. A `message` is an observable effect barrier
for functional optimization, as are `await`, domain-state access, I/O, and
other effects already listed in `docs/FUNCTIONAL_DATAFLOW.md`.

## Domains and legacy transport terminology

The source-level model is a statically checked domain computation. In new
prose, prefer **construct a domain instance** or **create a domain instance**.
`spawn` is the current source spelling and remains in executable examples and
the legacy backend; this phase does not invent a replacement constructor
syntax.

Mailbox, queue, worker, FIFO, and await descriptions in backend and historical
documents describe implementation choices or an earlier phase. They are not a
new source-level API. The native backend still documents those paths because
they remain implemented; Fast Debug does not simulate them yet.

For the converged source model, there is no self-send and no same-domain handler
chaining as a new supported language pattern. Common handler logic belongs in an
ordinary helper function. Historical material that discusses queued
self-transfers is retained as historical/legacy material pending a settled
language decision; removing or newly rejecting that behavior is **BLOCKED BY OPEN
DESIGN**. Likewise, concrete domain topology and
any domain rank (`rank_D`) are whole-program/link-time concerns, not local
handler facts, and Phase 10.5 does not choose their lowering.

Domain-local helper scoping belongs in the language/module design discussion,
not in an implicit dispatch rule. Existing ordinary functions and methods keep
their current lexical/module rules.

## Account synchronization derivation (documentation vocabulary)

The following is a complete derivation for the synchronization vocabulary used
by future design work. It is not an implementation claim and does not add
`ProtectedRead`, `LockSet`, or rank syntax to Moss.

```moss
domain Account:
  balance: Int

  fn deposit(delta: Int) -> Int:
    balance = balance + delta
    return balance

  fn snapshot() -> Int:
    return balance
```

Let `h` range over handlers and let `R(h)` and `W(h)` be the state fields read
and written by that handler:

```text
R(deposit)   = { balance }   W(deposit)   = { balance }
R(snapshot)  = { balance }   W(snapshot)  = { }
W* = union_h W(h) = { balance }
```

The conceptual signatures/classes are therefore:

```text
deposit  : (&mut AccountState, Int) -> Int   class Write
snapshot : (&AccountState) -> Int             class ProtectedRead
```

`ProtectedRead` is a future synchronization classification, not a Moss type.
It is justified only when the read's root belongs to `W*` and the future
whole-program synchronization proof permits that protected access. A read of a
non-`W*` root is not silently promoted to `ProtectedRead`; the final lock/rank
rules remain open.

For example, a conceptual pipeline capture can be written as:

```moss
# Illustrative capture shape; this does not add new state-access syntax.
values
  |> map(_ + account.balance)
```

The capture contributes a semantic domain READ. It is classified as
`ProtectedRead` only if the root `account` is in the whole-program `W*` set and
the future synchronization analysis proves the access. Phase 10.5 records the
vocabulary and provenance only; it does not fabricate a lock analysis.

## Synchronization diagnostics schema

The agent schema reserves a deterministic JSON shape for future synchronization
diagnostics without claiming that the current compiler has derived these facts:

```json
{
  "synchronization_diagnostics": {
    "availability": "schema_reserved_not_derived",
    "fields": [
      "class_count",
      "root_count",
      "handler_count",
      "conflicting_handler_pairs",
      "disjoint_handler_pairs",
      "collapse_culprit_fields"
    ],
    "semantics": "reserved until synchronization analysis is settled"
  }
}
```

No fake counts are emitted. The schema is available from
`moss agent schema --json` so tools can discover it without mistaking a
placeholder for a compiler result.

## Identity and tracing contract

Phase 5 source/provenance identities are deterministic for a build and retain
physical source locations; they are not promised to survive arbitrary source
movement. Phase 6 `entity-v1` identities are the separate semantic-query/edit
contract. Fast Debug trace events now carry the physical source file and the
corresponding semantic identity when the checked AST has it, alongside function,
line, branch, local-read/write, return, loop, and assertion events. Lock,
synchronization, and domain-rank event semantics remain **BLOCKED BY OPEN
DESIGN**.
