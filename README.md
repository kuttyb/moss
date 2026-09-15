# Moss compiler v0.2

This is the latest implemented Moss compiler currently available. It is a dependency-free C++17 front end that validates Moss source and emits standalone Rust.

Phase 2.5 is frozen and complete. Its mailbox, batching, direct-lock, RwLock,
atomic, and cluster lowerings are backend choices beneath one Moss semantic
model; later language or optimizer phases belong in separate checkpoints.

Phase 4 functional/dataflow compilation is now implemented on top of that frozen
foundation. Pipelines are typed Moss IR, callable effects are inferred separately
from ownership effects, and `-O` can fuse proven-safe stages into explicit Rust loops.
`-O0` remains the eager, stage-at-a-time semantic reference.

Phase 4.5 extends that same IR across a conservative lexical scope. It plans physical
materialization explicitly, simplifies exact counts, short-circuits safe `any`/`all`,
virtualizes single-use immutable pipeline bindings, and represents compatible terminal
consumers as a shared-source dataflow DAG. This adds no Moss syntax or lazy runtime.

Phase 4.6 adds a bounded semantic-space optimizer over that same authoritative IR. It
composes adjacent maps and filters, pushes a predicate only through a proven identity
map, removes safe dead maps before `count`, collapses single-use temporaries, and uses
terminal/cardinality facts before ordinary lowering. These are Moss-to-Moss rewrites,
not Rust iterator tricks or hardware-specific optimization.

Phase 4.7 adds statically resolved `for` loops. Ranges and vectors receive native
counted/indexed traversal, while user-defined iteration resolves to concrete
`next() -> Option[element]` methods under the static `Iterator` contract. Iteration
does not introduce runtime dispatch, vtables, boxing, or hidden copies; `while` remains
the general arbitrary loop.

Phase 5 adds tooling without changing those frozen semantics. Every compilation emits
one deterministic `.mossmap` provenance artifact shared by Emacs navigation, LLDB/DAP
source breakpoints, and symbol-targeted native disassembly. A dependency-light
`moss-mode` provides editing, checking, building, running, debugging, and generated-code
inspection from Moss source.

Phase 6 is frozen with a small, vendor-independent AI-native compiler workflow. Agents
can discover capabilities, receive structured diagnostics and repair alternatives, query
existing semantic/cost facts, compare durable semantic snapshots with `moss impact`,
make exact edits, format canonical Moss, and run only affected tests during iteration.
All of this is one versioned JSON envelope over the ordinary compiler truth; it adds no
duplicate semantic analysis or new language syntax. Start with
`moss agent bootstrap --json` and read [AI-native development](docs/AI_NATIVE_DEVELOPMENT.md).

Phase 7 is frozen with the practical project layer: `moss build`, `moss test`, and
`moss bench` share one manifest, compiler pipeline, profile model, semantic identity
scheme, artifact layout, and Phase 6A JSON protocol. Tests report Moss assertions at
Moss locations; benchmarks use release compilation, robust sampling,
toolchain-compatible saved comparisons, and complete discarded-value black boxing
without leaking Rust tooling into the normal workflow.

## Requirements

- A C++17 compiler (`g++`, `clang++`, or Apple Clang)
- `make`
- Rust (`rustc`) to compile the generated program
- Optional: Emacs 29+, LLDB with `lldb-dap`, Python 3, and `llvm-objdump` or
  GNU `objdump` for the corresponding Phase 5 integrations

## Install and project workflow

```sh
make
```

This creates `./moss`.

For the commands below, put the checkout on `PATH` with
`export PATH="$PWD:$PATH"` or replace `moss` with the absolute path to that
binary. Phase 7 does not yet provide an installer.

For a Moss project, the normal workflow is:

```sh
moss build
moss test
moss build --release
moss bench
```

Projects use `moss.toml` with conventional `src/`, `tests/`, and `benches/`
directories. Start with the practical [Moss project
workflow](docs/PROJECT_WORKFLOW.md): it provides a complete working project,
debug and release builds, cleaning and cache behavior, tests, benchmarks,
baselines, artifact locations, JSON output, and troubleshooting. The detailed
references are [the build system](docs/BUILD_SYSTEM.md), [unit
testing](docs/TESTING.md), and [benchmarking](docs/BENCHMARKING.md).

Projects without explicit modules retain the temporary global compilation unit:
`build` combines `src/**/*.moss`, `test` combines `src/**/*.moss` with
`tests/**/*.moss`, and `bench` combines `src/**/*.moss` with `benches/**/*.moss`.
Explicit modules instead compile into separate Rust crates/rlibs in import-DAG
order. Their `.mossi` interfaces carry Moss contracts and generic semantic IR;
typed imports need `.mossi` plus rlib, while generic imports specialize from
`.mossi` without provider source. Paths remain attached to diagnostics and
provenance.

Semantic queries and edits use that same temporary project context. A `src` source
queries all application files, a `tests` source queries `src + tests`, and a `benches`
source queries `src + benches`; standalone files outside a project remain single-file.

Automation and coding agents can discover the structured protocol first with
`moss agent bootstrap --json`.

The short agent loop is:

```sh
moss agent bootstrap --json
moss check src/main.moss --json
moss fmt
moss impact fn:changed --json
moss test --affected
```

See [the agent API](docs/AGENT_API.md) for durable IDs, semantic edits, repair actions,
cost facts, and structured output.

## Use

```sh
./moss --check examples/checkout.moss
mkdir -p build
./moss -Oshared-memory examples/checkout.moss -o build/checkout.rs
./moss -O --dump-functional-ir examples/functional_dataflow.moss -o build/dataflow.rs
rustc build/checkout.rs -o build/checkout
./build/checkout
```

Compilation also writes `build/checkout.mossmap`. It contains deterministic Moss
source/provenance identities, generated Rust ranges, concrete native symbols, and
functional fusion provenance. These Phase 5 identities are repeatable for an unchanged
source layout; Phase 6 exposes separate `entity-v1` durable semantic identities for
incremental analysis and exact edits.

Expected output:

```text
charged: 75
order completed
order rejected: insufficient inventory
```

Run the complete smoke test with `make check`.

For machine-readable compiler facts, bootstrap the Phase 6A protocol and use structured
checks or focused semantic queries:

```sh
./moss agent bootstrap --json
./moss check examples/functional_dataflow.moss --json
./moss effects fn:normalize --source examples/functional_dataflow.moss --json
./moss why main@12:expression:0 --source examples/functional_dataflow.moss --json -O
```

See [the agent semantic API](docs/AGENT_API.md) for selectors, diagnostic codes, and the
minimal recommended `AGENTS.md` onboarding snippet.

Compile every valid top-level example with the shared-memory optimization using `make examples-optimized` (or its alias `make examples`). Generated Rust and binaries are written under `build/examples/optimized`; the intentional negative `use_after_transfer.moss` example is skipped. Additional diagnostic examples live under `examples/errors` and are not part of this build target.

## What Moss looks like

Moss keeps types static while letting ordinary source stay compact: untyped functions
can require methods structurally, named traits are resolved to concrete call sites,
collections infer their contained types, and functional pipelines remain visible to
the compiler through typing, effect analysis, and optimization.
The showcase programs are executable syntax guides:

- [static duck typing](examples/static_duck_typing.moss) uses one method-based function with unrelated concrete types.
- [named traits](examples/traits.moss) calls one trait-typed function for Circle and Rectangle.
- [collections and methods](examples/collections_and_methods.moss) combines inferred Vector, Map, and Queue values with object methods.
- [functional dataflow](examples/functional_dataflow.moss) executes a typed `map |> filter |> map |> sum` chain that `-O` fuses into one explicit loop.
- [functional basics](examples/functional_basics.moss) combines named functions, placeholders, an immutable capture, and source reuse.
- [functional reductions](examples/functional_reductions.moss) demonstrates every terminal, empty-input identities, explicit-initializer `reduce`, and wrapping sums.
- [static functional callables](examples/functional_static_callables.moss) uses bound methods, method placeholders, and a higher-order helper specialized to two functions.
- [functional effect order](examples/functional_effect_order.moss) makes eager stage ordering and an effectful reduction initializer observable.
- [functional objects](examples/functional_objects.moss) runs statically resolved read-only methods and trivial field projections over user-defined values.
- [functional domains](examples/functional_domains.moss) shows an explicit `await` inside a callback acting as a fusion barrier, followed by a scalar message boundary.
- [functional terminal optimization](examples/functional_terminal_optimization.moss) shows count-to-length, dead pure-map removal, and safe `any`/`all` short-circuiting.
- [functional scope fusion](examples/functional_scope_fusion.moss) keeps a source-level immutable binding while removing its physical intermediate collection.
- [functional shared traversal](examples/functional_shared_traversal.moss) gives three independent terminals one stable-source traversal.
- [functional materialization](examples/functional_materialization.moss) shows a second consumer forcing a collection to exist before its terminal traversal is shared.
- [functional semantic optimization](examples/functional_semantic_optimization.moss) shows map/filter composition, a virtual named intermediate, dead-map removal, known-size count, and safe short circuiting.
- [mini application](examples/mini_application.moss) combines jobs, static dispatch, collections, domains, messages, awaits, and a pipeline.
- [Phase 2 safety](examples/phase2_safety.moss) demonstrates compatible read aliases, copyable projections, consuming method receivers, and explicit await/reply copy boundaries.
- [Phase 7 project](examples/projects/phase7_demo) demonstrates a manifest, debug/release builds, in-source and external tests, filtered benchmarks, and baselines.
- [failing Phase 7 test](examples/projects/phase7_failing_test) demonstrates Moss-level assertion details and continued test reporting.

Every valid showcase compiles to standalone Rust with `make examples`; the generated
programs contain concrete calls rather than runtime trait objects or vtables.

## Emacs, debugging, and disassembly

Load the bundled major mode directly from the repository:

```elisp
(add-to-list 'load-path "/path/to/moss/editors/emacs")
(require 'moss-mode)
```

From a `.moss` buffer, `M-x moss-check-buffer`, `moss-compile-buffer`, and
`moss-run-buffer` use Emacs compilation buffers with clickable Moss diagnostics.
`moss-show-generated-rust` and `moss-show-debug-map` open generated artifacts without
requiring their paths to be known.

For source debugging, run `M-x moss-build-debug-buffer`, add Moss breakpoints with
`M-x moss-toggle-breakpoint`, then launch `M-x moss-debug` through optional `dape` and
`lldb-dap`. Exact debug stops are mirrored back into Moss source through the shared map.
`M-x moss-disassemble-at-point` resolves the native symbol from that map and invokes
`llvm-objdump` or GNU `objdump`. See [Moss tooling and source
provenance](docs/TOOLING.md) for setup, LLDB commands, metadata format, and optimized
debugging limitations.

## Functional pipelines

The Phase 4 source surface includes `map`, `filter`, `reduce`, `sum`, `count`,
`any`, and `all`:

```moss
fn normalize(value: Int) -> Int:
  return value * 2

result = values
  |> map(normalize)
  |> filter(_ > 0)
  |> map(_ + 10)
  |> sum
```

The reference meaning is logically eager and source ordered: each complete `map` or
`filter` produces its logical result before the next stage starts. Ordinary
transformations READ their source and do not mutate it. `reduce` always takes an
explicit initial accumulator, so its empty-input result is defined; a nontrivial bound
initializer transfers into the reduction rather than being copied. Empty `sum` and
`count` produce zero, `any` produces false, and `all` produces true.
Initializer effects are part of the `Reduce` node and conservatively stop fusion, so
their eager evaluation order is identical under `-O0` and `-O`.

Named functions, statically bound instance methods such as `scaler.apply`, method
placeholders such as `_.score()`, and placeholder expressions with immutable captures
are supported. A higher-order helper
may accept an untyped callable parameter, but every call must bind it to one concrete
function identity; the compiler specializes the helper and erases that parameter
before Rust generation. There are no boxed callables, function-object vtables, or
runtime callable lookup.

`-O` fuses a chain only when its inferred observable effects make element-at-a-time
execution equivalent to the eager reference. I/O, local mutation, domain observation
or mutation, `message`, `await`, unresolved effects, and possible failure ordering are
barriers. A missed fusion is valid; a speculative reordering is not. Fused reductions
use Moss's wrapping integer arithmetic and allocate no intermediate vector.

Phase 4.5 applies the same proof facts to scope-level plans. Exact `count` becomes a
length query; preceding pure, non-failing, non-divergent maps can be removed when only
cardinality is used. Optimized `any` and `all` stop their own computation once known,
while the `-O0` reference and effectful, possibly failing, or potentially divergent
callbacks still visit every element. A
single-use immutable functional binding may remain virtual across the immediately
following consumer. Independent pure terminals over one unchanged local source may
share one explicit loop. Multiple uses, mutable bindings, intervening effects, source
mutation, dependencies between terminal results, and control flow conservatively keep
the relevant materialization or traversal boundary.

Potential divergence is distinct from failure and other observable effects. A callback
containing `while`, or transitively calling one that does, is conservatively marked
`may_diverge`; Moss does not claim to prove termination. This fact blocks optimizations
that skip invocations, but does not by itself block ordered fusion that preserves the
eager program's callback work.

Phase 4.6 makes the pre-lowering rewrite boundary explicit. Moss semantic-space
optimization is a Moss-to-Moss rewrite phase: it preserves a functional computation in
semantic form long enough to replace it with an equivalent Moss computation that asks
for less work. Adjacent pure maps become one composed map; adjacent pure filters become
one left-to-right, short-circuiting predicate; a safe trailing map whose values are
ignored by `count` disappears; and inert literal counts can become constants. A filter
may move through `map(_)` only for a trivial element type, where the existing typed IR
proves the map is identity. General predicate pushdown and algebraic rewriting remain
deferred because the compiler cannot yet prove them.

This is not lazy language semantics. Moss retains eager reference behavior under `-O0`;
the optimized plan merely avoids materialization or callback work when effects,
ownership, failure, and termination facts prove that difference unobservable. If an
optimization can be described as “this equivalent Moss computation asks for less
work,” it belongs here. SIMD, threading, GPUs, loop tiling, instruction selection, and
the existing domain-placement/locking choices belong downstream. Phase 4.6 adds no
MLIR or generic rewrite framework.

Semantic analysis attaches each expression's exact pipeline-plan ID to the checked AST;
code generation does not infer a plan again from matching source text. Numeric IR IDs are
transient compilation handles, while deterministic source/provenance identities carry
function/method context, real Moss lines, stage identity, and fusion provenance. Non-Copy
placeholder identity/projection maps are rejected at Moss level when they would move out
of the READ-only source, and capture mutation remains forbidden even when hidden behind
ordinary helper calls.

Inspect the compiler-owned representation and decisions with:

```sh
./moss -O --dump-functional-ir examples/functional_dataflow.moss -o build/dataflow.rs
./moss -O --explain-fusion tests/phase4_effect_smoke.moss --check
```

See [Functional/dataflow design and lowering](docs/FUNCTIONAL_DATAFLOW.md) for the
semantic and compiler contract.

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
- Inferred top-level `fn` functions, expression-bodied functions, and typed functional pipeline expressions
- Eager ordered `map`, `filter`, `reduce(initial, fn)`, `sum`, `count`, `any`, and `all`
- Static named/placeholder callables, immutable captures, and specialized higher-order helpers
- Effect-aware functional fusion and explicit-loop allocation elimination under `-O`
- Explicit functional materialization plans, count/short-circuit simplification,
  conservative cross-binding fusion, and shared-source terminal DAGs under `-O`
- Bounded Phase 4.6 semantic-stage rewrites for adjacent map/filter composition,
  identity-only predicate pushdown, terminal-aware dead work, and inert known-size count
- Versioned Phase 6A JSON bootstrap, structured diagnostics, and read-only semantic
  queries over existing compiler facts
- Deterministic `--dump-functional-ir` and `--explain-fusion` development diagnostics
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

Moss validates every `await` target in handlers, local helpers, and `main`, including
helpers reached only from `main`. A local domain-reference binding must have one
concrete static domain type after every control-flow join: assigning `Alpha` on one
branch and `Beta` on another is rejected rather than treated as a union or resolved by
branch order. Await traversal remains conservative, so an await inside `if false` still
contributes a dependency.

The compiler then builds a whole-program domain await DAG and rejects every possible
cycle. Dependencies propagate through ordinary non-recursive local function calls;
asynchronous `message` sends do not add edges. Cycle diagnostics show the Moss source
line for each await edge in the witness. Besides preventing logical deadlock under
serialized, non-reentrant domain semantics, this global acyclicity prevents cyclic
nested domain-lock acquisition in direct shared-memory lowering. A direct handler can
currently retain domain A's state lock for the full request/reply latency while it
awaits a mailbox-backed domain B. That is semantically correct—A remains occupied—but
is a future lock-hold-latency optimization opportunity. Cancellation, timeouts, and
failure propagation are not implemented.

## Domain clustering

Backend cluster configuration groups domain types onto one worker without adding Moss syntax:

```sh
./moss --cluster=Checkout,Inventory,Payments examples/checkout.moss -o build/checkout.rs
```

Each clustered type must currently be spawned exactly once and unconditionally in `main`. The generated runtime call creates one worker and one lock-backed ingress mailbox for the group. External calls use the shared-memory `_shared` implementation. Calls between cluster members are statically emitted as `_local` calls, and member capabilities become zero-sized local references, so there is no runtime placement check or shared-handle clone on that path.

Awaited local messages invoke the target handler directly. One-way local messages enter a plain single-threaded `VecDeque` and run after the current handler, retaining Moss's asynchronous and non-reentrant behavior without locks, atomics, or condition variables on the local path. If a local await follows an older queued message to the same target, the generated runtime drains that older work before making the direct call to preserve FIFO.

Await-cycle rejection is a language rule applied before backend placement, so the same source is rejected with or without `--cluster`. Cluster planning does not define a separate or weaker cycle policy.

## Important status

This is an early v0.2 prototype, not the compiler for the complete language we subsequently designed. It implements static duck-typed methods and named traits through concrete call-site specialization, Phase 4 typed functional/dataflow IR and conservative loop fusion, Phase 4.5 scope-level materialization/shared-traversal planning, and bounded Phase 4.6 semantic-space rewrites, without runtime trait or callable objects. It does not yet implement associated types, trait inheritance, default trait methods, source-level generics, automatic parallel/GPU lowering, later failure and cancellation semantics, blocking FFI rules, arenas, or a general multi-instance cluster planner.

Phase 2 local calls are non-recursive. The compiler rejects direct and mutual call cycles, infers READ/WRITE/CONSUME effects internally, and rejects conflicting access to the same storage location within one call. Moss exposes no ownership or effect annotations.

Messages, awaits, and replies are explicit value-copy boundaries: an object, collection, string, state value, or projection may cross a domain boundary, and the sender keeps its independent value. The compiler emits the required payload clone only at that explicit communication boundary, never for an ordinary local assignment or call. Large statically sized payloads produce a copy-cost warning. Direct assignment of a non-primitive local still transfers ownership; explicit `deepCopy()` for local duplication remains future work.

## Platforms

The source builds on Linux and macOS with a C++17 compiler. Build the compiler locally with `make`.
# Modules and separate compilation

Phase 8 adds first-class modules, qualified imports/exports, materialized typed
exports, semantic-IR generic exports, and `.mossi` module interfaces. See
[`docs/MODULES.md`](docs/MODULES.md) and
[`docs/MODULE_ABI.md`](docs/MODULE_ABI.md). Await analysis is keyed by declared
domain instances, never by domain type.
