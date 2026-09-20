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

Phase 10.6B.1 closes the routing universe: a handle is only a composition binding,
route binding, or `message` receiver. Historical examples that passed handles
to handlers/helpers or returned them through replies are superseded by declared
`domainroutes`. Ordinary data payload and reply-by-value semantics are unchanged.
Each graph instance refers explicitly to its concrete specialization record and
checked source domain declaration. Phase 10.6C derives a graph-relative
`SynchronizationPlan` from these exact specialization contexts.

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
for functional optimization, as are domain-state access, I/O, and
other effects already listed in `docs/FUNCTIONAL_DATAFLOW.md`.

## Domains and legacy transport terminology

The source-level model is a statically checked synchronous domain computation:
`message` is a blocking handler invocation, `reply` terminates a handler, and
source-level `await` is retired. In new prose, prefer **construct a domain instance**
or **create a domain instance**.
`spawn` is retired from source. Domains are constructed directly in `main`'s
initial composition prefix; `domainroutes(...)` declares immutable route slots.

Mailbox, queue, worker, FIFO, and await descriptions in backend and historical
documents describe implementation choices or an earlier phase. They are not a
new source-level API. The native backend still documents those paths because
they remain implemented; Fast Debug does not simulate them yet.

For the converged source model, self-send and same-domain handler chaining are
rejected. Common handler logic belongs in an ordinary helper function. Historical
material that discusses queued self-transfers is retained as legacy documentation.
Concrete domain topology and any domain rank (`domain_rank`) are whole-program
concerns, not local handler facts. Phase 10.6B validates the concrete route DAG
and assigns deterministic unique instance ordinals. Phase 10.6C derives
synchronization classes and local ranks; production 2PL lowering remains deferred.

Domain-local helper scoping belongs in the language/module design discussion,
not in an implicit dispatch rule. Existing ordinary functions and methods keep
their current lexical/module rules.

## Phase 10.6A synchronous-domain migration

The checked compiler now has one active domain-invocation concept: a
`message` is a synchronous, blocking handler call and may produce a value in
an expression. `reply` establishes that value and terminates the handler;
value-returning handlers must reply on every normal path. Source-level
`await` is retired and produces a migration diagnostic. Incoming payloads are
immutable READ snapshots: they may be read, forwarded through another
`message`, or replied by value, but cannot be written, rebound, or consumed.
Self-send and same-domain handler chaining are rejected; shared logic belongs
in ordinary helpers.

The Rust emitter still contains an isolated mailbox/completion adapter for
physical representations selected by the existing backend planner. It waits
for handler completion, so it does not expose asynchronous source behavior.
Direct construction and route topology are now checked by the Phase 10.6B
composition pass. Phase 10.6C implements synchronization planning. Production
2PL lowering, supervision, and Fast Debug domain execution remain deferred.

## Phase 10.6C synchronization planning

The compiler now derives one authoritative graph-relative `SynchronizationPlan`
from the closed concrete graph and exact specialized handler effects. It retains
READ / WRITE / CONSUME separately and maps them to shared/exclusive modes only
in the derived plan. Immutable-after-publication leaves receive no classes.
Production lock lowering is unchanged; handler-level 2PL remains Phase 10.6D.

See [SynchronizationPlan architecture](SYNCHRONIZATION_PLAN.md) for the definitions
of X, X*, ProtectedRead, leaf LockSet, class signatures, ClassSet, local ranks,
module metadata, Account golden, and the future scoped-domain breadcrumb.
Existing semantic queries expose both structured plan data and a human-readable
dump from the same object. The agent schema reports synchronization as derived.

## Identity and tracing contract

Phase 5 source/provenance identities are deterministic for a build and retain
physical source locations; they are not promised to survive arbitrary source
movement. Phase 6 `entity-v1` identities are the separate semantic-query/edit
contract. Fast Debug trace events now carry the physical source file and the
corresponding semantic identity when the checked AST has it, alongside function,
line, branch, local-read/write, return, loop, and assertion events. Lock,
synchronization, and domain-rank event semantics remain **BLOCKED BY OPEN
DESIGN**.
