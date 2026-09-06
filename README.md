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
./moss examples/checkout.moss -o build/checkout.rs
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
- `examples/object_pipeline.moss` creates and mutates an object inside a domain, passes it through read-only and mutating handlers, and finally sends it to another domain.
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

Each spawned domain still owns one OS thread and one `std::sync::mpsc` queue. An await blocks that domain's thread. The domain remains logically occupied and does not dequeue another message until the awaited reply arrives, so handlers are non-reentrant and queued messages retain serialized order. `await self.Message(...)` is rejected because it would necessarily deadlock.

Two or more domains can still deadlock if they form a cross-domain await cycle, such as A awaiting B while B awaits A. Such cycles can be data-dependent and are not detected in v0.2. Cancellation, timeouts, and failure propagation are also not implemented.

## Important status

This is an early v0.2 prototype, not the compiler for the complete language we subsequently designed. In particular, it does not yet implement later failure and cancellation semantics, blocking FFI rules, arenas, generics/traits, Domain Clustering, or message-elimination optimizations.

Detached nontrivial locals passed in messages are transferred and unavailable to the sender afterward. Domain state cannot be transferred by message, while domain-reference arguments remain usable by the sender. Hidden deep copies and copy-on-write are not part of Moss; explicit `deepCopy()` remains future work.

## Platforms

The source builds on Linux and macOS with a C++17 compiler. The included `bin/moss-linux-x86_64` is a convenience build for 64-bit Linux; macOS and Windows users should build from source.
