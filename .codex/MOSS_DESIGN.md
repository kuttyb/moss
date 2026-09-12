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
- Pipelines using `|>` are source syntax for nested local calls. The current frontend
  normalizes them before Rust lowering; optimization or fusion is a future backend choice.

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
- A nontrivial value already owned by a domain or by `main` cannot be transferred to another domain. This includes state, handler parameters, locals, and nontrivial fields reached through them:

```moss
let payload = buildPayload()
message worker.Process(payload) # compile-time error: payload cannot cross a domain boundary
```

- A value constructed directly as a message or reply payload is owned by that message and may cross the boundary. Primitive field projections can be used to construct a purpose-specific snapshot.
- Domain state and aliases into domain state cannot be transferred by message or reply. A nontrivial value that must exist independently on both sides requires the future explicit `deepCopy()` operation.
- Primitive values and domain references remain usable by the sender after a message send. A domain reference is a capability, not the domain's mutable state.
- For ownership checks, `int`, `float`, and `bool` are copy primitives; `option[T]` is copyable only when `T` is. Dynamically stored `string`, `seq`, `table`, and value-object data are non-primitive. Domain references follow the separate capability rule.

`self.Message(...)` is a queued message send to the current domain. It is not an ordinary synchronous procedure call:

```moss
message self.Continue(item)
```

The current handler completes before `Continue` can be dequeued. Because a self-message does not cross a domain boundary, it may transfer an owned local within the same domain. Top-level `fn` declarations and calls are now available for ordinary local computation; the future `proc` parameter model (including read-only and `var` parameters) remains deferred, and a self-message must not be used as a synchronous procedure substitute.

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

Cross-domain await cycles can deadlock. For example, A awaiting B while B awaits A is not detected statically because such cycles may be data-dependent. Cancellation, timeouts, and failure propagation are not defined in v0.2.

### Local bindings and mutation

- `let` introduces a local binding.
- `var` introduces a mutable local binding.
- Domain state and mutable object fields can be updated by the currently executing handler.
- Primitive expressions and conditions use the currently implemented Nim-style words `and`, `or`, `not`, `true`, and `false`.

Direct assignment of a uniquely owned nontrivial local transfers ownership. The source is unavailable after transfer, and a later use is a compile-time error. This is a Moss rule, not an inference from Rust's borrow checker.

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
- Domains with any asynchronous call retain the lock-backed queue-and-thread lowering. The optimization does not expose domain state, change any Moss call from asynchronous to synchronous, relax payload restrictions, alter FIFO guarantees, or make handlers reentrant.
- Each unpromoted handler lowers to a message-enum variant and a `DomainRef` send method. A promoted reply handler lowers to a lock-taking `DomainRef` method returning `Option<ReplyType>`, where `None` preserves the existing missing-reply failure.
- `--cluster=A,B` is a backend configuration, not Moss syntax. Each configured domain type must have exactly one unconditional spawn in `main`. The generated `spawn_moss_cluster_N` runtime call constructs one worker and one shared ingress mailbox for all members.
- Calls from outside a cluster use generated `_shared` methods and the lock-backed ingress mailbox. Calls between members are statically emitted as `_local` calls; there is no runtime cluster test. Cluster-member capabilities use zero-sized local reference types and convert to shared references only when they leave the cluster. Awaited local messages call the target handler directly. One-way local messages use a plain single-threaded `VecDeque` and are invoked after the current handler, preserving non-reentrancy.
- Before a direct local await, the generated runtime drains older local messages for that target. This prevents the direct call from overtaking an earlier message while allowing unrelated domains' local work to remain queued.
- A cluster configuration with a statically visible await cycle among its members is rejected; direct same-thread execution cannot represent the original blocked cycle without re-entering an active domain.
- Cluster state and the local queue use `RefCell` because only the cluster worker accesses them. Member-to-member dispatch contains no mutex, condition variable, atomic, or thread-safe channel operation; a handler that communicates outside its cluster still uses the shared path for that outbound call.
- The generated tracker counts enqueued messages so `main` can wait for quiescence.
- Existing owned nontrivial bindings are rejected at a cross-domain send or reply. Fresh message construction does not trigger a hidden copy of a domain-owned value.
- Domain-reference arguments are cloned as backend handles so the sender retains its capability; this is not immutable shared-value source semantics.
- A cross-thread await creates a one-shot lock-backed shared-memory cell, sends its handle with the request, and blocks on its condition variable. Blocking an OS thread is the current implementation of logical non-reentrancy, not a requirement for future runtimes.
- An ignored reply creates the same one-shot cell and immediately drops its receiver.
- `reply value` sends through the one-shot sender and exits the generated handler block. The domain tracker is completed once after the handler block.
- By default, generated await sites use a lazy `unwrap_or_else` failure path that reports a missing reply. `--no-await-error-handling` is an explicit backend opt-in that replaces those checks with unchecked extraction for a future supervision-tree runtime; it is unsafe if a reply is absent or its channel closes.

The current compiler enforces direct-assignment transfer, direct and nested owned payload boundaries, and state/reply restrictions with a lightweight ownership pass. It does not yet implement `deepCopy()` or complete ownership dataflow.

## Explicit deep copy

`deepCopy()` is the approved future Moss operation for independently duplicating a nontrivial value:

```moss
let duplicate = original.deepCopy()
```

It is not implemented in v0.2. The compiler must not silently insert it. A future implementation should classify copies containing strings, sequences, tables, or transitively dynamic objects as potentially unbounded and warn accordingly.

### Ordinary intra-domain procedures

The approved future parameter model is:

```moss
proc inspect(value: Order)
proc prepare(value: var Order)
```

Read-only parameters do not consume the caller's value; `var` parameters permit temporary caller-visible mutation without transferring ownership. The syntax and implementation are not yet available. In particular, `self.Message(...)` must not be documented or implemented as a substitute for a synchronous procedure call.

## Deferred future work

- General immutable cross-domain sharing (`share`, `freeze`, or source-level reference counting) is deferred because of memory retention and leak concerns.
- Arena handles and `ref object` identity semantics are deferred because unreachable graphs, cycles, and small handles retaining large graphs need a clear reclamation model.
- Persistent versions and `revise` with structural sharing are deferred for memory-retention evaluation. This is distinct from copy-on-write and is not implemented.
