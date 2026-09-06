# Moss language design

This document is the authoritative record of approved Moss source-language semantics. Backend details are recorded separately so implementation choices do not accidentally become language rules.

Rules under **Source-level semantics** are programmer-facing language decisions. Deferred future work is explicitly non-authoritative and must not be inferred as current syntax or behavior.

## Source-level semantics

### Program structure

- Moss source uses two-space indentation. Tabs are rejected.
- Top-level declarations currently consist of value-object types, domains, and `proc main()`.
- A value-object type declares named fields:

```moss
type WorkItem = object
  name: string
  revisions: int
```

- A domain owns mutable state and declares message handlers. Domain state is only accessed while that domain is executing a handler.
- `main` may spawn domains. Spawning from a handler is not supported in v0.2.
- The implemented source types are `int`, `float`, `bool`, `string`, domain references, value-object types, `seq[T]`, `option[T]`, and `table[K, V]`.

### Domains and messages

A one-way handler omits a reply type:

```moss
on Notify(text: string)
  echo text
```

A standalone call to a domain handler is an asynchronous message send:

```moss
worker.Notify("ready")
```

Approved message semantics are:

- Each domain processes at most one handler at a time.
- A handler runs to completion unless it executes `return` or `reply`.
- Messages already queued for a domain are not processed reentrantly during the current handler.
- Messages from one sender to one receiving domain preserve FIFO order.
- Message payloads have value semantics. A receiver cannot use a payload to mutate the sender's local value.
- Sending a detached, uniquely owned nontrivial local transfers it to the receiving domain. The sender cannot use that local afterward:

```moss
let payload = buildPayload()
worker.Process(payload)
echo payload       # compile-time error: payload was transferred
```

- Domain state and aliases into domain state cannot be transferred by message. A state field must be copied explicitly in a future `deepCopy()` operation or converted into a purpose-specific snapshot.
- Primitive values and domain references remain usable by the sender after a message send. A domain reference is a capability, not the domain's mutable state.

`self.Message(...)` is a queued message send to the current domain. It is not an ordinary synchronous procedure call:

```moss
self.Continue(item)
```

The current handler completes before `Continue` can be dequeued. Moss does not yet define ordinary intra-domain procedure declarations or calls.

### Request/reply handlers

A handler may declare one reply type:

```moss
on Contains(key: string) -> bool
  reply true
```

For v0.2:

- The declared reply type must be a valid Moss type.
- A reply-capable handler must contain at least one syntactic `reply` statement. The compiler does not yet prove that every control-flow path replies.
- `reply expression` is valid only in a reply-capable handler.
- `reply` sends one response and terminates the current handler.
- Bare `reply`, `reply` in `main`, and `reply value` in a one-way handler are errors.
- `return` remains value-less. `return value` is an error.
- Reaching the end of an awaited handler without replying is a runtime failure reported to the awaiter.

### Await

In v0.2, `await` is allowed only as the complete initializer of a local `let` or `var`:

```moss
let reserved = await inventory.Reserve(quantity)
var answer = await worker.Compute(42)
```

The source-level rules are:

- The receiver must resolve to a domain reference.
- The handler must exist, accept the supplied number of arguments, and declare a reply type.
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
- Every spawned domain currently uses one OS thread and one `std::sync::mpsc` queue.
- Each handler lowers to a message-enum variant and a `DomainRef` send method.
- The generated tracker counts enqueued messages so `main` can wait for quiescence.
- A detached nontrivial local passed as a message argument is lowered as an ownership transfer. No hidden deep copy is inserted.
- Domain-reference arguments are cloned as backend handles so the sender retains its capability; this is not immutable shared-value source semantics.
- A reply-capable message carries a one-shot `std::sync::mpsc::Sender<ReplyType>`.
- An await creates a one-shot channel, sends its sender with the request, and blocks on the receiver. Blocking an OS thread is the current implementation of logical non-reentrancy, not a requirement for future runtimes.
- An ignored reply creates the same one-shot channel and immediately drops its receiver.
- `reply value` sends through the one-shot sender and exits the generated handler block. The domain tracker is completed once after the handler block.

The current compiler enforces the approved direct-assignment and detached-message transfer cases with a lightweight ownership pass. It does not yet implement `deepCopy()` or complete ownership dataflow.

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
