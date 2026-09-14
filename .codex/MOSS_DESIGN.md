# Moss language design

This document is the authoritative record of approved Moss source-language semantics. Backend details are recorded separately so implementation choices do not accidentally become language rules.

Rules under **Source-level semantics** are programmer-facing language decisions. Deferred future work is explicitly non-authoritative and must not be inferred as current syntax or behavior.

## Source-level semantics

### Program structure

- Moss source uses two-space indentation. Tabs are rejected.
- Top-level declarations consist of value-object types, local `fn` functions, domains, and
  either `fn main()` or the compatible `proc main()` entry point.
- A trailing `:` is accepted on indentation-oriented domain and handler headers; it is
  syntax sugar for the existing block structure.
- A value-object type declares named fields:

```moss
type WorkItem:
  name: string
  revisions: int
```

The legacy `type WorkItem = object` spelling remains accepted. A field annotation may be
omitted when constructor and use constraints infer one concrete type; an unresolved field
is a compile-time error.

- Domain state uses value-binding syntax. An initializer normally supplies the static
  type, while an annotation remains an optional constraint:

```moss
domain Counter:
  value = 0
  limit: Int = 10
```

An uninitialized or otherwise unconstrained state field is a compile-time error. The
legacy `var value: int = 0` spelling remains accepted during migration.

- A top-level `fn` is an ordinary local function. Expression-bodied functions use
  `fn square(x) = x * x`; block functions return their final expression. Parameter and
  result annotations are optional only when static inference resolves them.
- Pipelines using `|>` are typed functional/dataflow expressions. They retain their
  source stages in compiler IR until Moss either materializes a collection or lowers a
  terminal computation. They are not runtime Rust iterator or callable objects.

- A domain owns mutable state and declares message handlers. Domain state is only accessed while that domain is executing a handler.
- Message handler parameter annotations may be omitted when whole-program calls infer one
  concrete contract; an unresolved handler parameter is a compile-time error.
- `main` may spawn domains. Spawning from a handler is not supported in v0.2.
- The implemented source types are `int`, `float`, `bool`, `string`, domain references, value-object types, `seq[T]`, `option[T]`, and `table[K, V]`.

### Domains and messages

A handler with no explicit reply annotation is inferred as one-way when it has no
`reply`. If it contains typed reply expressions, their common type becomes the handler's
reply type; an explicit `-> Type` remains an optional constraint:

```moss
fn Latest():
  reply Quote(symbol = "MOSS", price = 12.5)
```

Named object constructors use `=` for value bindings. The older `field: value`
constructor spelling remains accepted as a migration compatibility form.

A domain handler must be called with an explicit communication form:

```moss
message worker.Notify("ready")
```

A naked `worker.Notify(...)` call is rejected and must use `message` or `await`. A call
such as `square(value)` targets a local function and has no domain scheduling meaning.

Approved message semantics are:

- Each domain processes at most one handler at a time.
- A handler runs to completion unless it executes `return` or `reply`.
- Messages already queued for a domain are not processed reentrantly during the current handler.
- Messages from one sender to one receiving domain preserve FIFO order.
- Message payloads have value semantics. A receiver cannot use a payload to mutate the sender's local value.
- A message, await request, or reply is an explicit value-copy boundary. Nontrivial state, locals, parameters, and projections may cross it, and the sender retains its independent value:

```moss
let payload = buildPayload()
message worker.Process(payload) # `worker` receives an independent payload value
```

- This boundary copy is part of the semantics of `message`, `await`, and `reply`; it is not a hidden copy inserted for an ordinary local expression. The compiler warns when a payload has a statically known size above 1024 bytes. Dynamically sized payloads have no fixed estimate in this initial implementation.
- A value constructed directly as a message or reply payload follows the same value semantics. Primitive field projections remain useful for purpose-specific, smaller snapshots.
- Primitive values and domain references remain usable by the sender after a message send. A domain reference is a capability, not the domain's mutable state.
- For ownership checks, `int`, `float`, and `bool` are copy primitives; `option[T]` is copyable only when `T` is. Dynamically stored `string`, `seq`, `table`, and value-object data are non-primitive. Domain references follow the separate capability rule.

`self.Message(...)` is a queued message send to the current domain. It is not an ordinary synchronous procedure call:

```moss
message self.Continue(item)
```

The current handler completes before `Continue` can be dequeued. A self-message also creates its explicit payload value, so sender and receiver do not share a mutable object. Top-level `fn` declarations and calls are now available for ordinary local computation; the future `proc` parameter model (including read-only and `var` parameters) remains deferred, and a self-message must not be used as a synchronous procedure substitute.

### Request/reply handlers

A handler may declare one reply type or let the compiler infer it from its `reply`
expressions:

```moss
fn Contains(key: string):
  reply true
```

For v0.2:

- The declared or inferred reply type must be a valid Moss type.
- A reply-capable handler must contain at least one syntactic `reply` statement. The compiler does not yet prove that every control-flow path replies.
- Every `reply` expression must have the declared or inferred handler reply type. Conflicting reply paths are compile-time errors.
- `reply` sends one response and terminates the current handler.
- Bare `reply` and `reply` in `main` are errors. A handler with an inferred reply is no longer one-way.
- `return` remains value-less in domain handlers. Local `fn` functions may return a value.
- Reaching the end of an awaited handler without replying is a runtime failure reported to the awaiter.

### Await

In v0.2, `await` is allowed only as the complete right-hand side of an assignment:

```moss
reserved = await inventory.Reserve(quantity)
let answer = await worker.Compute(42)
```

The source-level rules are:

- The receiver must resolve to a domain reference.
- The handler must exist, accept the supplied number of arguments, and have a declared or inferred reply type.
- The destination local's type is inferred from the handler's reply type.
- The awaiting domain remains logically occupied. It cannot process another message until the reply arrives and the current handler resumes.
- `await self.Message(...)` is rejected because a non-reentrant domain cannot service its own queued request while waiting.
- Calling a reply-capable handler without `await` is allowed and means send the request but ignore its reply.
- General expression nesting such as `1 + await worker.Compute()` is not supported in v0.2.

Static await-target validation applies to every handler, local function, object method,
and `main`, whether or not a domain handler reaches that code. The type environment is
control-flow aware: it forks for `if`/`else` and merges at the join. A local binding is
available as a domain reference after the join only when it exists on every incoming
path and every path agrees on one concrete domain type. Divergent `Alpha`/`Beta`
bindings are rejected; v0.2 has no union or domain-sum reference type.

After target validation, the compiler builds a conservative await-dependency graph
across every domain and handler. Dependencies reached through ordinary local function
calls participate; asynchronous `message` sends do not. Every possible cycle is a
compile-time error even when a runtime branch appears unreachable, and diagnostics
identify the Moss line for each await edge in the cycle witness. Local call cycles are
rejected first, which keeps interprocedural await traversal finite; memoization avoids
repeated work. Adding recursion would require an explicit redesign of await-effect
propagation.

The global await DAG is also a shared-memory backend soundness invariant. A direct
handler may hold its domain state lock across a nested await that acquires another
domain lock, so global acyclicity prevents cyclic nested lock acquisition and lock-order
inversion. Cancellation, timeouts, and failure propagation are not defined in v0.2.

### Local bindings and mutation

- `let` introduces a local binding.
- `var` introduces a mutable local binding.
- Domain state and mutable object fields can be updated by the currently executing handler.
- Primitive expressions and conditions use the currently implemented Nim-style words `and`, `or`, `not`, `true`, and `false`.

Direct assignment of a uniquely owned nontrivial local transfers ownership. The source is unavailable after transfer, and a later use is a compile-time error. This is a Moss rule, not an inference from Rust's borrow checker.

Local functions and object methods are non-recursive in Phase 2. READ, WRITE, and CONSUME effects are compiler-internal summaries used to lower temporary reads, mutations, and ownership transfers. At each local call, overlapping storage may be read more than once, but mutation or transfer may not overlap any other access. No effect or ownership annotation is part of Moss syntax.

### Functional/dataflow expressions

The implemented Phase 4 operations are:

- `map(callable)` and `filter(callable)`, which logically produce new collections;
- `sum`, `count`, `any`/`any(callable)`, and `all`/`all(callable)` terminals;
- `reduce(initial, callable)`, whose explicit initial value also defines empty-input
  behavior.

Their reference semantics are eager and ordered. A complete transformation stage runs in
source-element order and logically materializes before the next stage begins. `map` and
`filter` READ their source by default and do not mutate or consume it. Empty `sum` and
`count` yield zero, empty `any` yields false, empty `all` yields true, and empty `reduce`
yields its initial accumulator. Integer accumulation follows Moss's wrapping `Int`
semantics. A nontrivial bound `reduce` initializer transfers into the reduction and is
unavailable afterward. This lets the empty-input result own the original accumulator
without an implicit clone; primitive initializers retain ordinary copy behavior.

Functional callables are closed statically. The implemented forms are named functions,
bound READ-only instance methods such as `scaler.apply`, statically resolved
placeholder-method calls such as `_.score()`, and `_` placeholder expressions that may
read immutable local captures. An untyped higher-order parameter is
valid only when each use specializes to a concrete callable identity at its call site.
There is no boxed function value, `dyn Fn`, runtime method lookup, dynamic fallback, or
new source-level generic category.

Ordinary ownership rules remain authoritative inside a pipeline. The input collection is
normally READ and remains usable afterward. A stage's parameter and captures retain their
inferred READ/WRITE/CONSUME requirements; Moss rejects a consuming element operation or
mutable capture when current container semantics cannot express it without an implicit
copy or unsafe alias. A transformation pipeline cannot cross `message`, `await`, or
`reply` directly; a programmer must first bind its materialized Moss collection. A
terminal already produces a concrete scalar and may use the ordinary explicit copy
boundary. No boundary transports a lazy or compiler-internal pipeline value.

Observable callable effects are distinct from ownership effects. The compiler records
local capture reads, local mutation, domain reads and writes, messages, awaits, external
or I/O effects, unresolved effects, possible failure, and potential divergence. The last
fact is separate: a syntactic `while` (including one reached through a local call) is
conservatively `may_diverge`; Moss does not prove loop termination. Fusion is allowed
only when changing eager stage-at-a-time execution into element-at-a-time execution
cannot make any of those effects or their failure order observable. Missing an
optimization is valid; reordering without proof is not.

Phase 4.5 keeps these eager source semantics and adds no functional syntax. The `-O0`
reference evaluates all `any`/`all` predicates in order. `-O` may short-circuit only
pure, non-failing, non-divergent work, and may replace exact `count` (including through
dead pure maps) with collection length only when eliminated callbacks are likewise safe
to skip. Potential divergence does not alone block ordinary fusion that preserves
required callback work and whose changed inter-stage schedule is otherwise unobservable.
Materialization is planned separately from loop fusion. A
single-use immutable intermediate can remain virtual across one adjacent consumer, while
multiple consumers, escape, mutation, control flow, ownership requirements, or an
observable/failure barrier force a concrete collection.

Independent pure terminal consumers over the same unchanged local collection can form a
shared-source DAG and execute in one traversal. This first rule is lexical and
conservative: consumers are adjacent, results are new locals, paths cannot fail or have
effects, and no terminal depends on another terminal's result. Phase 4.5 performs no
generic stage reordering or predicate pushdown.

Phase 4.6 is a bounded semantic-space optimization pass over the same typed functional
IR. “Semantic-space optimization” means a Moss-to-Moss rewrite: preserve the functional
computation as Moss semantics long enough to replace it with an equivalent Moss
computation that asks for less work before ordinary lowering. It is not lazy evaluation
and does not change the eager `-O0` reference contract.

The optimized semantic-stage sequence can compose adjacent safe maps, compose adjacent
safe filters with left-to-right short circuiting, eliminate safe trailing maps whose
values are unused by `count`, retain only cardinality-changing work for terminal count,
and preserve/consume single-use virtual bindings. An inert scalar vector literal may
supply a constant count without construction. `any`/`all` continue to use Phase 4.5's
strict work-skipping rule.

Predicate pushdown is initially limited to the proof already expressible by the IR:
`map(_)` over a trivial element is identity, so a following filter may operate on the
earlier value and the map may disappear. A named or non-identity map is not speculatively
rewritten. General predicate algebra is deferred.

Every rewrite uses the existing ownership, observable-effect, `may_fail`, and
`may_diverge` facts. Invocation-preserving composition may retain potentially divergent
callbacks; a rewrite that executes fewer callbacks requires them to be safe to skip.
Original source nodes and provenance remain intact even when their semantic work is
removed. The pass is a small explicit sequence, not MLIR or a general optimization
framework.

## Rust lowering

This section describes the current v0.2 backend. It is not a source-language contract unless the same rule is stated above.

- Moss emits standalone Rust and adds no external Rust dependency.
- `int`, `float`, `bool`, and `string` lower to `i64`, `f64`, `bool`, and `String`.
- `seq`, `option`, and `table` lower to `Vec`, `Option`, and `HashMap`.
- Value-object types lower to Rust structs currently deriving `Clone` and `Debug`.
- Cross-thread message and reply transport always lowers to generated shared memory: `Arc<Mutex<VecDeque<T>>>`-style storage with a `Condvar`. Generated Rust does not use `std::sync::mpsc`.
- Every value that enters a worker thread is checked at a generated Rust `Send` boundary.
- An unclustered queued domain owns one worker thread. Its reference writes to the lock-backed mailbox, and its worker drains the shared queue serially.
- With `-Oshared-memory` (or `-O`), a backend-only whole-program pass may eliminate awaited request/reply transport for a domain when all of its handlers reply, every call to it is awaited, and its state and message types satisfy the supported `Send` analysis.
- An eligible domain lowers to `Arc<Mutex<DomainState>>`. The awaiting caller locks that state, executes the handler to completion on its physical thread, and receives the result directly. Generated Rust assertions retain the `Send` boundary.
- Domains with any asynchronous call retain the lock-backed queue-and-thread lowering. The optimization does not expose domain state, change any Moss call from asynchronous to synchronous, alter explicit payload-copy semantics, alter FIFO guarantees, or make handlers reentrant.
- Each unpromoted handler lowers to a message-enum variant and a `DomainRef` send method. A promoted reply handler lowers to a lock-taking `DomainRef` method returning `Option<ReplyType>`, where `None` preserves the existing missing-reply failure.
- `--cluster=A,B` is a backend configuration, not Moss syntax. Each configured domain type must have exactly one unconditional spawn in `main`. The generated `spawn_moss_cluster_N` runtime call constructs one worker and one shared ingress mailbox for all members.
- Calls from outside a cluster use generated `_shared` methods and the lock-backed ingress mailbox. Calls between members are statically emitted as `_local` calls; there is no runtime cluster test. Cluster-member capabilities use zero-sized local reference types and convert to shared references only when they leave the cluster. Awaited local messages call the target handler directly. One-way local messages use a plain single-threaded `VecDeque` and are invoked after the current handler, preserving non-reentrancy.
- Before a direct local await, the generated runtime drains older local messages for that target. This prevents the direct call from overtaking an earlier message while allowing unrelated domains' local work to remain queued.
- Await-cycle rejection is a placement-independent language check. A program containing a possible cycle is rejected before either clustered or unclustered lowering.
- A direct-shared handler currently retains its source-domain state lock while a nested
  await completes, including the full request/reply latency of a mailbox-backed target.
  This physically enforces the source domain's logical non-reentrancy and is correct,
  but the potentially long lock hold is a future performance optimization concern.
- Cluster state and the local queue use `RefCell` because only the cluster worker accesses them. Member-to-member dispatch contains no mutex, condition variable, atomic, or thread-safe channel operation; a handler that communicates outside its cluster still uses the shared path for that outbound call.
- The generated tracker counts enqueued messages so `main` can wait for quiescence.
- Every non-copy payload is cloned at a `message`, `await`, or `reply` boundary, including an existing binding or a projection. This is the explicit Moss value-copy boundary, not a hidden local copy.
- Domain-reference arguments are cloned as backend handles so the sender retains its capability; this is not immutable shared-value source semantics.
- A cross-thread await creates a one-shot lock-backed shared-memory cell, sends its handle with the request, and blocks on its condition variable. Blocking an OS thread is the current implementation of logical non-reentrancy, not a requirement for future runtimes.
- An ignored reply creates the same one-shot cell and immediately drops its receiver.
- `reply value` sends through the one-shot sender and exits the generated handler block. The domain tracker is completed once after the handler block.
- By default, generated await sites use a lazy `unwrap_or_else` failure path that reports a missing reply. `--no-await-error-handling` is an explicit backend opt-in that replaces those checks with unchecked extraction for a future supervision-tree runtime; it is unsafe if a reply is absent or its channel closes.
- Before Rust emission, Phase 4 constructs typed functional/dataflow nodes for `Source`,
  `Map`, `Filter`, `Reduce`, `Sum`, `Count`, `Any`, and `All`. Nodes use transient numeric
  IR IDs for compiler lookup while separately retaining a stable semantic/source identity,
  Moss source span, concrete input/output types, callable identity, ownership and
  observable-effect summaries, logical materialization, and optimization provenance.
- `-O0` lowers pipelines as explicit eager stage loops and materializations, preserving
  the language reference execution. `-O` fuses a proven-safe chain into one simple loop
  and eliminates only its otherwise logical intermediate collections. Moss performs this
  transformation before Rust generation; it does not delegate the pipeline to a Rust
  `.iter().map().filter()` chain.
- A fused loop retains the semantic identities of every source stage it represents. The deterministic
  `--dump-functional-ir` and `--explain-fusion` diagnostics expose types, spans, effects,
  materialization decisions, provenance, and barriers for compiler development and
  regression tests.
- Phase 4.5 gives every logical transformation an explicit virtual/materialized plan and
  reason. It can link one immutable producer binding to its sole consumer, or create a
  `FunctionalTraversalGroup` whose one source has several terminal consumer edges.
  Generated Rust executes these exact plan IDs as one ordinary loop and retains all
  contributing semantic identities as provenance.
- Phase 4.6 gives each pipeline a post-analysis semantic-stage sequence. Composed steps
  refer back to all original nodes, eliminated steps remain in provenance, and terminal
  rewrites are selected before Rust emission. The emitter executes this exact optimized
  semantic sequence rather than rediscovering it from source text.

The current compiler enforces direct-assignment and nontrivial-projection transfer, conflicting call-access rejection, branch/join consumption, and explicit communication-boundary copying with a Moss-level ownership/effect pass. It does not yet implement `deepCopy()` or general reference/alias facilities.

## Explicit deep copy

`deepCopy()` is the approved future Moss operation for independently duplicating a nontrivial value:

```moss
let duplicate = original.deepCopy()
```

It is not implemented in v0.2. The compiler must not silently insert it for ordinary local code; message, await, and reply remain the explicit value-copy operations. A future implementation should classify copies containing strings, sequences, tables, or transitively dynamic objects as potentially unbounded and warn accordingly.

### Ordinary intra-domain procedures

The approved future parameter model is:

```moss
proc inspect(value: Order)
proc prepare(value: var Order)
```

Read-only parameters do not consume the caller's value; `var` parameters permit temporary caller-visible mutation without transferring ownership. The syntax and implementation are not yet available. In particular, `self.Message(...)` must not be documented or implemented as a substitute for a synchronous procedure call.

## Deferred future work

- Automatic SIMD, threading, and GPU/accelerator lowering are deferred. Phase 4/4.5/4.6 record
  element independence, determinism, captures, reduction compatibility, and topology so
  later planners can prove such choices without changing the functional source contract.
- General lambdas, runtime function values, user-facing effect annotations, initializer-
  free reduction, generic stage reordering/algebraic predicate pushdown, and a functional
  cost model are deferred. Phase 4/4.5/4.6 do not add a
  dynamic callable runtime or exception/failure semantics.
- General immutable cross-domain sharing (`share`, `freeze`, or source-level reference counting) is deferred because of memory retention and leak concerns.
- Arena handles and `ref object` identity semantics are deferred because unreachable graphs, cycles, and small handles retaining large graphs need a clear reclamation model.
- Persistent versions and `revise` with structural sharing are deferred for memory-retention evaluation. This is distinct from copy-on-write and is not implemented.
