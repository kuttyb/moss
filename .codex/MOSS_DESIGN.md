# Moss language design

This document is the authoritative record of approved Moss source-language semantics. Backend details are recorded separately so implementation choices do not accidentally become language rules.

Only rules under **Source-level semantics** are programmer-facing language decisions. Items under **Unresolved semantics** are deliberately not authoritative.

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
- A domain reference is a capability to send that domain messages; it does not expose the domain's state.

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

The semantics of assigning a nontrivial local value to another local are unresolved; see **Unresolved semantics**. No backend ownership behavior is authoritative for Moss until this is decided.

## Rust lowering

This section describes the current v0.2 backend. It is not a source-language contract unless the same rule is stated above.

- Moss emits standalone Rust and adds no external Rust dependency.
- `int`, `float`, `bool`, and `string` lower to `i64`, `f64`, `bool`, and `String`.
- `seq`, `option`, and `table` lower to `Vec`, `Option`, and `HashMap`.
- Value-object types lower to Rust structs currently deriving `Clone` and `Debug`.
- Every spawned domain currently uses one OS thread and one `std::sync::mpsc` queue.
- Each handler lowers to a message-enum variant and a `DomainRef` send method.
- The generated tracker counts enqueued messages so `main` can wait for quiescence.
- Non-copy message payloads are currently cloned before enqueueing. Cloning is a backend strategy used to implement Moss message value semantics, not a requirement that Moss programmers request clones.
- A reply-capable message carries a one-shot `std::sync::mpsc::Sender<ReplyType>`.
- An await creates a one-shot channel, sends its sender with the request, and blocks on the receiver. Blocking an OS thread is the current implementation of logical non-reentrancy, not a requirement for future runtimes.
- An ignored reply creates the same one-shot channel and immediately drops its receiver.
- `reply value` sends through the one-shot sender and exits the generated handler block. The domain tracker is completed once after the handler block.

The current compiler lowers a direct nontrivial local assignment using Rust assignment and contains a provisional checker that treats this as an ownership transfer. That behavior is an implementation experiment, not approved Moss semantics.

## Unresolved semantics

### Nontrivial local assignment

No authoritative meaning has been chosen for:

```moss
let moved = original
```

The open choices are move, copy, alias, or value assignment with compiler-selected copy-on-write or equivalent representation. The chosen rule must be based on observable Moss behavior rather than what is easiest to emit in Rust.

Until Kutty decides, the ownership-transfer diagnostic currently emitted by the compiler is provisional and must not be used as evidence of the language rule.

### Ordinary intra-domain procedures

Moss does not yet have an approved declaration or call model for ordinary synchronous functions owned by a domain. In particular, `self.Message(...)` must not be documented or implemented as a substitute for a synchronous procedure call.

