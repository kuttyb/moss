# Fast Debug execution

Fast Debug executes the already checked Moss representation directly, without
generating Rust or invoking `rustc` for Moss code.

```text
moss run --interp program.moss
moss run --interp --trace program.moss
moss test --interp tests/fast_debug.moss
moss debug app
margo debug
margo debug --trace
```

For a project, `moss debug` accepts the project name (`app`, the manifest name,
or `.`), a project directory, or an entry `.moss` file. It uses the same checked
project analysis as native builds. Explicit-module projects interpret the
transitive source-module closure; legacy projects use their complete
`src/**/*.moss` uber-module. A source-free `.mossi` dependency cannot be mixed
into interpreted execution: Fast Debug rejects it with
`FAST_DEBUG_NATIVE_DEPENDENCY`; use native execution or make the dependency source
available through Margo's resolved package graph. `moss debug` does not resolve Margo
path/Git dependencies itself. For a package graph use `margo debug` or `margo debug
--trace`: Margo resolves packages, Moss resolves modules and semantics, and Fast Debug
interprets the reachable Moss source closure. Phase 10.6E adds no Rust interoperability
or new foreign-call ABI.

The interpreter shares the parser, checker, effects, ownership rules, concrete
domain graph, and specialization identities. It executes ordinary functions,
methods, locals, structs, vectors and indexing, arithmetic, conditionals, while
loops, and assertions. Inferred WRITE parameters refer to caller storage,
including primitive parameters and nested domain-state projections. Integer
arithmetic retains Moss wrapping behavior.

Closed enum construction, exhaustive READ and consuming matches, owned-rvalue
consumption, `replace` on writable state, `pass`, and `var` reinitialization use
the same checked semantics in Fast Debug and native execution. `message` and
`reply` establish value boundaries for READ enum payload views. Traces report
the selected case and domain state reads/writes, including a replacement.

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
aggregate summaries avoid dumping entire collections.

Phase 22.4 adds deterministic execution-local event identities and causal links
to every trace event:

```json
{
  "event_id": 12,
  "parent_event_id": 9,
  "call_event_id": 8,
  "message_event_id": 10,
  "handler_event_id": 11,
  "control_event_id": 7,
  "depth": 3
}
```

These IDs are stable for one deterministic Fast Debug execution with the same
inputs. They are not global runtime identities. They connect trace events back to
compiler-owned source/semantic identities and preserve the interpreter's real
nested structure for function calls, method calls, branch/loop bodies, and
synchronous `message` handler execution. No runtime addresses, class ranks, lock
events, or simulated interleavings appear in this trace. When traced execution
fails, Fast Debug still flushes the structured events recorded before the error.

## Structured debug queries

`moss debug-query` executes the checked program once under Fast Debug and returns
a bounded JSON slice of that execution. `--json` is required; the command has no
second human-output protocol:

```sh
moss debug-query failure-slice --source app.moss --json
moss debug-query event event:42 --source app.moss --json --before 3 --after 3
moss debug-query semantic entity-v1:function:normalize --source app.moss --json
moss debug-query message-subtree handler:Service.Run --source app.moss --json
moss debug-query writes Account.balance --source app.moss --json --before-event 84
moss debug-query control-flow event:84 --source app.moss --json
```

Supported operations are `event`, `semantic`, `subtree`, `message-subtree`,
`control-flow`, `writes`, and `failure-slice`. Selectors include `event:<id>`,
compiler semantic identities such as `entity-v1:function:normalize`, source
identities such as `fn:normalize@12`, `handler:Domain.Name`,
`instance:<concrete-instance-id>`, `local:<binding>`, and domain state paths.
A bare state path is accepted only when it identifies one dynamically relevant
concrete instance. Qualify ambiguous state as
`<concrete-instance-id>.<path>` (for example `main::account.balance`). A local
name is accepted only when its matching writes belong to one callable semantic
scope. Ambiguous write selectors fail with
`DEBUG_QUERY_SELECTOR_AMBIGUOUS`; the API does not combine unrelated locals or
domain instances.

Every result reports its bounds and truncation status:

```json
{
  "truncated": true,
  "returned_events": 20,
  "available_more": true,
  "bounds": {"max_events": 20, "max_depth": 8}
}
```

The query interface deliberately provides recent Moss-visible writes, not full
dynamic taint provenance or heap snapshots. If a semantic entity exists but did
not execute, the command reports `DEBUG_QUERY_NOT_EXECUTED`; unknown events and
malformed selectors use stable `DEBUG_QUERY_*` error codes. This is not an
interactive debugger: there are no `step`, `next`, `continue`, breakpoint, or
watchpoint commands.

`debug-query --source` uses Moss's standalone or same-project source context.
It does not resolve Margo path/Git dependencies. `margo debug [--trace]` remains
the package-resolved Fast Debug path, but it currently exposes the full trace,
not a second structured-query implementation. A structured query therefore
requires the complete reachable source closure to be available in the source
context supplied directly to Moss.

## Echo display contract

`echo a, b, ...` prints its arguments separated by one space and ends the
line. Fast Debug and native execution (Debug and Release) print the same text
for every argument the checker accepts:

| Type | Text |
| --- | --- |
| `Int` | decimal, with `-` for a negative value (`-9223372036854775808`) |
| `Float` | canonical Moss Float text: shortest round-trip digits, `1.5`, `100.0`, `-0.0`, `0.1`, `1e21`, `1e-7`, `NaN`, `inf`, `-inf` |
| `Bool` | `true` or `false` |
| `Char` | the character itself, unquoted |
| `String` | the text itself, unquoted and unescaped |
| `Vector[T]` | `[`, each element's text by this table, separated by `, `, then `]` |

An empty Vector prints `[]`. Nested Vectors use the same rule recursively
(`[[1], [], [2, 3]]`). A Float element prints the same text as `echo` of that
Float alone. String and Char elements are raw, so `["a, b"]` and `["a", "b"]`
both print `[a, b]`. They are raw because this matches scalar `echo`. Quoting
would need an escape grammar that Moss does not define yet. Echo the elements
one at a time when the form must be unambiguous.

Native code formats a Vector through the generated Moss display runtime
(`__moss_display_vector`). The compiler picks each element's formatter
statically from the checked Moss type, with no runtime trait or dynamic
dispatch. It never uses Rust's `Debug` representation, so the text is a Moss
contract and not a property of the Rust standard library.

Any other type has no display contract. The checker rejects it with
`ECHO_DISPLAY_UNSUPPORTED` before any engine runs: `Map`, `Queue`, records,
enums, function references, a Unit call result, and a Vector whose element
type is any of these. `Map` and `Queue` have no contract in this cycle. Map
iteration order is unspecified, so a Map has no deterministic text. A Queue
form could be added later as an explicit decision. Domain handles
and `range(...)` values are rejected earlier by their own rules. The FileIO
byte `Range` keeps its Phase 20 binary-safe `b"..."` display. This is
native-only, because Fast Debug rejects FileIO programs, and a Range is not a
Vector element form. Collections have no `to_string()` and no `+`
concatenation with a String, so no other path formats a collection.

`moss check` gives the same verdict for a source file whether it is checked
standalone or as part of a project. Project-mode check stops at semantics,
like standalone check, and does not run native lowering. Before Phase 25 S-B,
project check lowered to Rust and could reject a file that standalone check
accepted (P25-S-ARCH-019).

## Remaining interpreter limits

`for` traversal over ranges, Vectors, and Iterator values runs with native
block scoping. A Queue or a bare Map is not a `for` source in either engine.
A program whose `main` can reach FileIO is rejected with `FAST_DEBUG_FILEIO`
before any statement executes. In test mode (`moss test --interp`,
`margo test --interp`) each selected test block is its own entry point: a test
whose reachable closure can reach FileIO is reported as
`FAIL <name>: [FAST_DEBUG_FILEIO] ...` without executing any of its
statements, the remaining tests run normally, and the run exits 1. The native
test runner (`margo test`) executes FileIO tests. Reachability is a static
over-approximation: every function, method, handler, or domain that a
reachable body could name is followed, except where the name provably cannot
execute code, namely a String or Char literal, a `label:` (constructor field,
`by:`), a case or recover pattern, an enum case (`Kind.Name`), a parameter,
field, or in-scope local in value position, or a member name that can only be a
method or handler. When the checker resolved a message to a specific handler,
only that handler is followed, so a same-named handler of another domain does
not cause a rejection. Current eager functional pipelines run
in Fast Debug. There is no blanket domain/message/reply restriction or automatic native
Moss fallback. Source-free providers remain supported by production, while Fast
Debug requires their source.

Fast Debug is a full peer of native execution: a program that passes `moss
check` must produce the same results in both engines. The Phase 24 divergences
(`for` traversal, `Float.to_string()`, and `Int` ordering above 2^53) are fixed,
as are the Phase 25 review findings on IEEE Float `==`, range bounds at `Int`
max, the former 10,000,000-iteration cap, and block scoping. Report any remaining
difference as a defect. Phase 10.6F records [production measurements and trace-size observations](PERFORMANCE_10_6F.md);
concurrency correctness remains covered by the production harness.
