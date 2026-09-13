# Moss compiler v0.2

This is the latest implemented Moss compiler currently available. It is a dependency-free C++17 front end that validates Moss source and emits standalone Rust.

Phase 2.5 is frozen and complete. Its mailbox, batching, direct-lock, RwLock,
atomic, and cluster lowerings are backend choices beneath one Moss semantic
model; later language or optimizer phases belong in separate checkpoints.

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

Compile every valid top-level example with the shared-memory optimization using `make examples-optimized` (or its alias `make examples`). Generated Rust and binaries are written under `build/examples/optimized`; the intentional negative `use_after_transfer.moss` example is skipped. Additional diagnostic examples live under `examples/errors` and are not part of this build target.

## What Moss looks like

Moss keeps types static while letting ordinary source stay compact: untyped functions
can require methods structurally, named traits are resolved to concrete call sites,
collections infer their contained types, and pipelines remain readable nested calls.
The showcase programs are executable syntax guides:

- [static duck typing](examples/static_duck_typing.moss) uses one method-based function with unrelated concrete types.
- [named traits](examples/traits.moss) calls one trait-typed function for Circle and Rectangle.
- [collections and methods](examples/collections_and_methods.moss) combines inferred Vector, Map, and Queue values with object methods.
- [functional dataflow](examples/functional_dataflow.moss) records the `map |> filter |> map |> sum` source shape that future fusion can target.
- [mini application](examples/mini_application.moss) combines jobs, static dispatch, collections, domains, messages, awaits, and a pipeline.
- [Phase 2 safety](examples/phase2_safety.moss) demonstrates compatible read aliases, copyable projections, consuming method receivers, and explicit await/reply copy boundaries.

Every valid showcase compiles to standalone Rust with `make examples`; the generated
programs contain concrete calls rather than runtime trait objects or vtables.

Additional examples:

- `examples/counter.moss` demonstrates serialized state updates.
- `examples/shared_memory.moss` shows ordinary Moss messages and awaits selecting mailbox or whole-domain atomic implementations without changing the source.
- `examples/frontend_syntax.moss` demonstrates inferred `fn` functions, `type Name:` fields, pipelines, explicit messages, and assignment-await syntax.
- `examples/object_pipeline.moss` creates and mutates an object inside one domain, passes it through that domain's handlers, and sends a primitive snapshot to another domain.
- `examples/use_after_transfer.moss` demonstrates the approved ownership-transfer rule for nontrivial local values.

The intentional programs under `examples/errors` showcase Phase 2 diagnostics rather
than Rust backend failures:

- [global await cycle](examples/errors/await_cycle.moss) hides one dependency behind an ordinary function call and an unreachable runtime branch.
- [recursive local call](examples/errors/recursive_call.moss) has a base case but is rejected because Phase 2 has no recursion.
- [conflicting call access](examples/errors/conflicting_access.moss) passes one binding as both WRITE and READ; two READ uses remain legal in `phase2_safety.moss`.

Inspect them directly:

```sh
./moss --check examples/errors/await_cycle.moss
./moss --check examples/errors/recursive_call.moss
./moss --check examples/errors/conflicting_access.moss
```

To inspect the ownership failure:

```sh
./moss --check examples/use_after_transfer.moss
```

Moss reports the later use and the line where ownership transferred:

```text
moss:12: error: value 'original' was transferred to 'destination' at line 10. Create an explicit deep copy if both values must remain independently usable.
```

This is a Moss source rule: assigning a nontrivial uniquely owned local transfers it, and Moss never inserts a hidden deep copy in ordinary local code. The approved `deepCopy()` operation is not implemented yet; see `.codex/MOSS_DESIGN.md` for the current contract and deferred work.

## Implemented language slice

Moss `Int` is currently a signed 64-bit two's-complement value. Integer
arithmetic has explicit wrapping semantics: overflow is reduced modulo
2<sup>64</sup> and interpreted again as a signed `Int`. In particular, integer
`+`, `-`, and `*`, integer collection `sum`, and generated state-update
equivalents all wrap. Integer division also wraps its sole signed-overflow case
(`Int` minimum divided by `-1`); division by zero remains invalid. Generated
Rust uses explicit wrapping operations, so Rust debug/release overflow settings
cannot change Moss results. Atomic add/sub handlers use the same rule when
deriving a returned new value.

- Domain-owned mutable state
- Serialized, run-to-completion domain handlers
- Cross-domain asynchronous message sends
- Explicit `message domain.Handler(...)` syntax for asynchronous sends
- Request/reply handlers with inferred or optional `-> Type` reply annotations
- Domain state with inferred `name = initializer` bindings and optional `name: Type` constraints
- `reply value`, which sends one response and terminates the current handler
- `value = await domain.Message(...)`, plus compatible `let`/`var` await declarations
- Inferred top-level `fn` functions, expression-bodied functions, and pipeline expressions
- Colon-style `type Name:` declarations with statically inferred field types
- `spawn` from `main`
- Primitive and object value payloads
- `let`, `var`, `if`/`else`, `while`, `echo`, and bare `return`
- Nim-style `and`, `or`, `not`, `true`, and `false`
- `int`, `float`, `bool`, `string`, `seq`, `option`, and `table` lowering

## Await and reply

A handler without `-> Type` is inferred as one-way when it has no `reply`. If it contains
typed `reply` expressions, Moss infers their common reply type. An explicit `-> Type`
remains an optional constraint:

```moss
fn Reserve(quantity: int) -> bool:
  if available >= quantity:
    available = available - quantity
    reply true
  reply false
```

Domain state uses the same inference rule:

```moss
domain Counter:
  value = 0
```

Named object constructors use `=` for value bindings:

```moss
Quote(symbol = "MOSS", price = 12.5)
```

For v0.2, `await` is supported as the complete right-hand side of an assignment:

```moss
reserved = await inventory.Reserve(quantity)
```

The `let reserved = await ...` and `var reserved = await ...` forms remain compatible.
`await` is a Moss domain request/reply operation, not general coroutine syntax.

Ordinary local functions use `fn` and can omit types when inference is unambiguous:

```moss
fn square(x) = x * x

fn score(x):
  x |> square
```

`message` and `await` are required at domain boundaries. A naked `domain.Handler(...)`
call is rejected by the compiler; a call without a domain receiver remains an ordinary
local function call.

Reply handlers may also be called without `await`; the message is sent normally and its reply is ignored. If an awaited handler reaches its end without executing `reply`, the awaiting code fails with a clear runtime error.

`--no-await-error-handling` omits the generated per-await `unwrap_or_else` diagnostic path and uses unchecked reply extraction instead. This is intended for a future supervision-tree runtime that owns failures; until those guarantees exist, the default await checks should remain enabled.

By default, each spawned domain owns one OS thread and a generated lock-backed shared-memory mailbox. An await blocks that domain's thread. The domain remains logically occupied and does not dequeue another message until the awaited reply arrives, so handlers are non-reentrant and queued messages retain serialized order. `await self.Message(...)` is rejected because it would necessarily deadlock.

## Shared-memory message transport

`-Oshared-memory` (or `-O`) runs a whole-program backend planner that selects the
cheapest implementation it can prove equivalent:

```sh
./moss -Oshared-memory examples/checkout.moss -o build/checkout.rs
```

The plan records one domain lowering—`Mailbox`, `DirectMutex`, `DirectRwLock`,
`DirectAtomic`, or configured `ClusterLocal`—plus separate batched-send and
coalesced-lock regions. Rust generation executes that plan rather than rediscovering
optimization patterns. A configured cluster takes precedence, followed by a legal
whole-domain atomic representation, direct/coalesced shared state, RwLock or Mutex,
batched mailbox transport, and finally an ordinary mailbox.

- Adjacent side-effect-free asynchronous messages to the same receiver use one queue
  lock and one completion-tracker update. Payload values are still copied at the Moss
  message boundary and retain source order.
- Awaited-only domains can use direct shared state. Handler implementation is split
  from its lock wrapper, and a uniquely owned sequence of awaits in `main` can reuse
  one guard. If another caller or escaped capability is possible, each operation keeps
  its own guard.
- A state-reading handler with no ordering-sensitive external effect can take a shared
  `RwLock` guard. State writes remain exclusive; write-only or uncertain domains use
  `Mutex`.
- A domain made entirely of one-action integer or boolean handlers uses `AtomicI64`
  and `AtomicBool` with `SeqCst` ordering. Eligible loads, stores, add/subtract,
  toggles, and swaps execute directly for both `message` and `await`, so a fully
  atomic domain has no worker, mailbox, condition variable, or state mutex. One
  ineligible handler makes the whole domain fall back to locking.

These are physical lowering choices only. Domains still logically serialize handlers;
sender FIFO and a valid domain-wide total order remain intact; `message`, `await`, and
`reply` remain semantic copy boundaries; and an awaiting handler remains non-reentrant.
No optimization inserts `unsafe` or synchronization syntax into Moss. `-O0` retains
the ordinary lock-backed mailbox implementation as the semantic reference.
Boundary regressions compile that reference and the optimized atomic backend with
Rust overflow checks enabled and require identical results at `i64::MIN` and
`i64::MAX`.

Generated Rust is annotated for inspection: `Moss line N` identifies the source line
for a directly corresponding declaration or statement. `Moss backend plan` records
each domain classification, while `Moss backend` marks atomic handlers, shared reads,
coalesced guards, batched enqueues, and cluster-local calls.

Moss builds a conservative whole-program await-dependency graph and rejects every possible domain cycle at compile time. Await dependencies propagate through ordinary local function calls; asynchronous `message` sends do not add dependency edges. An await target must resolve to a conservatively bounded domain set. Cancellation, timeouts, and failure propagation are not implemented.

## Domain clustering

Backend cluster configuration groups domain types onto one worker without adding Moss syntax:

```sh
./moss --cluster=Checkout,Inventory,Payments examples/checkout.moss -o build/checkout.rs
```

Each clustered type must currently be spawned exactly once and unconditionally in `main`. The generated runtime call creates one worker and one lock-backed ingress mailbox for the group. External calls use the shared-memory `_shared` implementation. Calls between cluster members are statically emitted as `_local` calls, and member capabilities become zero-sized local references, so there is no runtime placement check or shared-handle clone on that path.

Awaited local messages invoke the target handler directly. One-way local messages enter a plain single-threaded `VecDeque` and run after the current handler, retaining Moss's asynchronous and non-reentrant behavior without locks, atomics, or condition variables on the local path. If a local await follows an older queued message to the same target, the generated runtime drains that older work before making the direct call to preserve FIFO.

Await-cycle rejection is a language rule applied before backend placement, so the same source is rejected with or without `--cluster`. Cluster planning does not define a separate or weaker cycle policy.

## Important status

This is an early v0.2 prototype, not the compiler for the complete language we subsequently designed. It implements the initial static duck-typed method and named-trait foundation by generating concrete call-site specializations, without runtime trait objects. It does not yet implement associated types, trait inheritance, default trait methods, source-level generics, later failure and cancellation semantics, blocking FFI rules, arenas, or a general multi-instance cluster planner.

Phase 2 local calls are non-recursive. The compiler rejects direct and mutual call cycles, infers READ/WRITE/CONSUME effects internally, and rejects conflicting access to the same storage location within one call. Moss exposes no ownership or effect annotations.

Messages, awaits, and replies are explicit value-copy boundaries: an object, collection, string, state value, or projection may cross a domain boundary, and the sender keeps its independent value. The compiler emits the required payload clone only at that explicit communication boundary, never for an ordinary local assignment or call. Large statically sized payloads produce a copy-cost warning. Direct assignment of a non-primitive local still transfers ownership; explicit `deepCopy()` for local duplication remains future work.

## Platforms

The source builds on Linux and macOS with a C++17 compiler. Build the compiler locally with `make`.
