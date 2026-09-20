# Fast Debug execution

Fast Debug executes the already checked Moss representation directly, without
generating Rust or invoking `rustc` for Moss code.

```text
moss run --interp program.moss
moss run --interp --trace program.moss
moss test --interp tests/fast_debug.moss
moss debug app
```

For a project, `moss debug` accepts the project name (`app`, the manifest name,
or `.`), a project directory, or an entry `.moss` file. It uses the same checked
project analysis as native builds. Explicit-module projects interpret the
transitive source-module closure; legacy projects use their complete
`src/**/*.moss` uber-module. A source-free `.mossi` dependency cannot be mixed
into interpreted execution: supply the reachable Moss source or use a native
build. Phase 10.6E adds no Rust interoperability or new foreign-call ABI.

The interpreter shares the parser, checker, effects, ownership rules, concrete
domain graph, and specialization identities. It executes ordinary functions,
methods, locals, structs, vectors and indexing, arithmetic, conditionals, while
loops, and assertions. Inferred WRITE parameters refer to caller storage,
including primitive parameters and nested domain-state projections. Integer
arithmetic retains Moss wrapping behavior.

## Synchronous domains

Phase 10.6E adds logical instances corresponding one-to-one with the checked
ConcreteDomainGraph. Each has its exact specialization, state store, and immutable
route bindings. Composition initializes state before ordinary execution. Routes
resolve to concrete instances from the checked graph, including forward source
bindings and multiple specializations of the same declaration.

`message` evaluates its arguments, establishes independent payload values, and
executes the target handler synchronously in a nested interpreter frame. `reply`
establishes an independent result and immediately terminates that frame. State
WRITE changes the logical domain store; helpers and methods use the existing
checked READ/WRITE/CONSUME effects. Incoming payload legality is enforced by the
checker, before interpretation. Nested calls and later sibling calls execute in
literal synchronous order.

The production and Fast Debug engines intentionally have different physical
representations:

| Engine | Execution |
| --- | --- |
| Production | Callable concurrently; SynchronizationPlan determines exact class RwLocks, modes, global rank order, full-handler retention, and borrowed state views. |
| Fast Debug | One deterministic synchronous execution; logical values and nested frames, with no locks, scheduler, contention simulation, or synchronization-plan dependency. |

Neither engine adds recovery/supervision semantics. An interpreter error stops
the invocation. Future lexical domain scopes remain open design; interpreter
instances belong to an execution, not process-global singletons.

## Structured Execution Trace

`--trace` writes newline-delimited JSON to standard error. Existing function,
method, local-read/write, branch, return, loop, and assertion events remain.
Domain events use these names:

```text
domain_instance
message_call → handler_enter
                state_read / state_write / state_consume
                reply → handler_exit
message_return
```

Events carry source file/line and semantic identity. Domain events also identify
the concrete instance, specialization, and handler. State events carry access
paths, including paths reached through helpers and methods. Writes include
bounded before/after summaries. Details are limited to 256 characters and
aggregate summaries avoid dumping entire collections. These stable identities
allow future instance/handler/message-subtree trace slicing. No runtime addresses,
class ranks, lock events, or simulated interleavings appear in this trace.

## Remaining interpreter limits

Existing unsupported expression/iteration constructs, including functional
pipelines and `for` traversal, still produce construct-specific interpreter
errors. There is no blanket domain/message/reply restriction or automatic native
Moss fallback. Source-free providers remain supported by production, while Fast
Debug requires their source. Quantitative production layout/performance work is
Phase 10.6F; concurrency correctness remains covered by the production harness.
