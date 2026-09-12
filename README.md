# Moss compiler v0.2

This is the latest implemented Moss compiler currently available. It is a dependency-free C++17 front end that validates Moss source and emits standalone Rust.

## Requirements

- A C++17 compiler (`g++`, `clang++`, or Apple Clang)
- `make`
- Rust (`rustc`) to compile the generated program

## Build

```sh
make
```

This creates `./moss`.

## Use

```sh
./moss --check examples/checkout.moss
mkdir -p build
./moss -Oshared-memory examples/checkout.moss -o build/checkout.rs
rustc build/checkout.rs -o build/checkout
./build/checkout
```

Expected output:

```text
charged: 75
order completed
order rejected: insufficient inventory
```

Run the complete smoke test with `make check`.

Additional examples:

- `examples/counter.moss` demonstrates serialized state updates.
- `examples/shared_memory.moss` shows ordinary Moss messages and awaits lowering to a lock-backed shared-memory mailbox, with an optional direct `Arc<Mutex<_>>` optimization.
- `examples/object_pipeline.moss` creates and mutates an object inside one domain, passes it through that domain's handlers, and sends a primitive snapshot to another domain.
- `examples/use_after_transfer.moss` demonstrates the approved ownership-transfer rule for nontrivial local values.

To inspect the ownership failure:

```sh
./moss --check examples/use_after_transfer.moss
```

Moss reports the later use and the line where ownership transferred:

```text
moss:12: error: value 'original' was transferred to 'destination' at line 10. Create an explicit deep copy if both values must remain independently usable.
```

This is a Moss source rule: assigning a nontrivial uniquely owned local transfers it, and Moss never inserts a hidden deep copy. The approved `deepCopy()` operation is not implemented yet; see `.codex/MOSS_DESIGN.md` for the current contract and deferred work.

## Implemented language slice

- Domain-owned mutable state
- Serialized, run-to-completion domain handlers
- Cross-domain asynchronous message sends
- Typed request/reply handlers declared with `on Name(...) -> Type`
- `reply value`, which sends one response and terminates the current handler
- `let value = await domain.Message(...)` and `var value = await domain.Message(...)`
- `spawn` from `main`
- Primitive and object value payloads
- `let`, `var`, `if`/`else`, `while`, `echo`, and bare `return`
- Nim-style `and`, `or`, `not`, `true`, and `false`
- `int`, `float`, `bool`, `string`, `seq`, `option`, and `table` lowering

## Await and reply

A handler without `-> Type` is one-way. A handler that declares a reply type must contain at least one `reply` statement:

```moss
on Reserve(quantity: int) -> bool
  if available >= quantity:
    available = available - quantity
    reply true
  reply false
```

For v0.2, `await` is supported only as the complete initializer of a `let` or local `var`:

```moss
let reserved = await inventory.Reserve(quantity)
```

Reply handlers may also be called without `await`; the message is sent normally and its reply is ignored. If an awaited handler reaches its end without executing `reply`, the awaiting code fails with a clear runtime error.

By default, each spawned domain owns one OS thread and a generated lock-backed shared-memory mailbox. An await blocks that domain's thread. The domain remains logically occupied and does not dequeue another message until the awaited reply arrives, so handlers are non-reentrant and queued messages retain serialized order. `await self.Message(...)` is rejected because it would necessarily deadlock.

## Shared-memory message transport

`-Oshared-memory` (or `-O`) runs a whole-program backend pass that can eliminate eligible request/reply messages:

```sh
./moss -Oshared-memory examples/checkout.moss -o build/checkout.rs
```

The pass promotes a domain only when all of its handlers reply, every call to that domain is awaited, and its state, parameters, and replies are safe at Rust's `Send` boundary. Its generated reference then owns `Arc<Mutex<DomainState>>`. An await locks that state, runs the handler to completion, and returns the reply directly. Calls from different domain threads may contend on the state lock, but no two handlers can access the state concurrently.

This remains only a Rust lowering choice. Moss continues to treat the operation as a message and retains the same payload, ordering, run-to-completion, and non-reentrancy rules. A domain with any one-way handler, ignored reply, unresolved call, or other asynchronous send keeps its lock-backed shared-memory mailbox and dedicated thread. `-O0` disables direct dispatch while retaining that shared-memory transport.

Generated Rust is annotated for inspection: `Moss line N` identifies the source line for a directly corresponding declaration or statement, while `Moss backend` comments identify the selected message/mailbox, shared-memory direct, or cluster-local lowering.

The promoted handler runs on the awaiting caller's physical thread; physical thread identity is not part of Moss's approved semantics. Because the caller was already required to block and the target has no asynchronous callers, this does not turn an asynchronous source operation into a synchronous one. A local contention smoke benchmark is included in the tests, but production transport choices should still be based on representative workloads.

Two or more domains can still deadlock if they form a cross-domain await cycle, such as A awaiting B while B awaits A. Such cycles can be data-dependent and are not detected in v0.2. Cancellation, timeouts, and failure propagation are also not implemented.

## Domain clustering

Backend cluster configuration groups domain types onto one worker without adding Moss syntax:

```sh
./moss --cluster=Checkout,Inventory,Payments examples/checkout.moss -o build/checkout.rs
```

Each clustered type must currently be spawned exactly once and unconditionally in `main`. The generated runtime call creates one worker and one lock-backed ingress mailbox for the group. External calls use the shared-memory `_shared` implementation. Calls between cluster members are statically emitted as `_local` calls, and member capabilities become zero-sized local references, so there is no runtime placement check or shared-handle clone on that path.

Awaited local messages invoke the target handler directly. One-way local messages enter a plain single-threaded `VecDeque` and run after the current handler, retaining Moss's asynchronous and non-reentrant behavior without locks, atomics, or condition variables on the local path. If a local await follows an older queued message to the same target, the generated runtime drains that older work before making the direct call to preserve FIFO.

The initial planner rejects clusters with a statically visible await cycle between members. Such a cycle requires one active clustered domain to wait for another call that would eventually re-enter it; unclustered Moss retains its existing potential to deadlock on the same program.

## Important status

This is an early v0.2 prototype, not the compiler for the complete language we subsequently designed. In particular, it does not yet implement later failure and cancellation semantics, blocking FFI rules, arenas, generics/traits, or a general multi-instance cluster planner.

An existing non-primitive local, parameter, state value, or non-primitive projection cannot be transferred across a domain boundary, including between clustered domains. A fresh value constructed directly as a payload is message-owned, primitive snapshots may cross, and domain-reference arguments remain usable by the sender. Queued self-messages stay within one domain and may transfer a local. Hidden deep copies and copy-on-write are not part of Moss; explicit `deepCopy()` remains future work.

## Platforms

The source builds on Linux and macOS with a C++17 compiler. Build the compiler locally with `make`.
