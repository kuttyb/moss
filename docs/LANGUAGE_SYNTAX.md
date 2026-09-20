# Moss language syntax

This document records the source-level direction for the Moss frontend. It describes
Moss semantics separately from compiler planning and Rust lowering. The compiler is a
statically compiled language implementation; Rust is an implementation and checking
backend, not part of the Moss surface language.

## Design direction

Moss aims for Julia-like ergonomics, OCaml-like inference, static AOT certainty, Rust
backend checking, and the existing Moss domain semantics. Types are optional to spell,
never optional to know. When inference cannot determine one unambiguous static type or
semantic fact, compilation fails. The compiler must not recover by introducing dynamic
values, runtime dispatch, or an implicit `Any` type.

## Lexical and indentation rules

Moss uses significant indentation with two spaces per block level. Tabs are rejected.
Comments begin with `#` outside a string. A trailing colon is accepted on block headers;
it does not change the block's meaning. The frontend may retain compatibility spellings
while the syntax migration proceeds, but new examples should use the forms documented
here.

## Static typing and inference

Parameters are either untyped or typed. Untyped means statically duck typed.
Typed means a concrete type or a trait. There is no dynamic typing and no
source-level generic `T`; all required type and operation relationships are
resolved and verified at compile time.

An annotation constrains and documents an inferred type:

```moss
fn distance(a: Point, b: Point) -> Float:
    dx = a.x - b.x
    dy = a.y - b.y
    sqrt(dx*dx + dy*dy)
```

The same function may omit annotations when its complete meaning is inferable:

```moss
fn distance(a, b):
    dx = a.x - b.x
    dy = a.y - b.y
    sqrt(dx*dx + dy*dy)
```

Inference is static and whole-program where necessary. Literal types, operators,
field projections, constructors, function results, and declared domain handlers provide
constraints. An unresolved parameter, field, return type, or call is a compile-time
error. A failed inference must produce a Moss diagnostic rather than a Rust dynamic
fallback.

The initial migration supports the existing primitive types and value-object types.
Inference will grow incrementally; a construct is accepted only when the compiler can
materialize a concrete type for the generated Rust representation.

## Value types

The preferred type declaration is an indented block:

```moss
type Quote:
    symbol: string
    price: float
    size: int
    ts: int
```

Field annotations may be omitted when constructor use and other constraints determine
them:

```moss
type Quote:
    symbol
    price
    size
    ts
```

An omitted field type is an inference request, not a dynamic field. Ambiguous or unused
fields must fail compilation until an annotation or an unambiguous constraint is
provided. The legacy `type Quote = object` spelling remains accepted during migration
and has the same value-object semantics.

Domain state uses the same value-binding convention. An initializer normally supplies
the type:

```moss
domain Counter:
    value = 0
```

An annotation remains an optional constraint:

```moss
domain Counter:
    value: Int = 0
```

An uninitialized or otherwise unconstrained state field is a compile-time error. The
compatibility spelling `var value: int = 0` remains accepted, but new source should use
the binding form above.

Named object construction uses `=` for value bindings:

```moss
Quote(symbol = "MOSS", price = 12.5)
```

The older `Quote(symbol: "MOSS", price: 12.5)` form remains accepted during migration.
The colon continues to mark type constraints in declarations and parameters.

## Functions and expressions

Top-level local functions use `fn` and can be written as an expression or an indented
block:

```moss
fn square(x) = x * x

fn normalize(x):
    total = sum(x)
    x / total
```

The final expression of a block is its result. Explicit `return value` may be retained
for migration and early exits. A call such as `normalize(data)` is an ordinary local,
intra-domain call. It has no mailbox, request/reply, or domain scheduling meaning.

`fn main()` is the preferred spelling for the program entry point; `proc main()` remains
a compatibility spelling while existing programs migrate. Domain message handlers keep
their serialized handler semantics. Both `on Handler(...)` and `fn Handler(...)` inside a
`domain` declare message handlers; the `message` or `await` keyword at each call site
still makes the domain boundary explicit. A top-level `fn` declares an ordinary local
function. Domain and handler headers may carry a trailing `:` when using indentation
oriented formatting. Handler parameter types, like local function parameters, may be
inferred from whole-program message calls when one concrete message contract results;
an unresolved handler parameter remains a compile-time error. A handler with no `-> Type`
annotation is inferred as one-way when it has no `reply`; when it contains typed reply
expressions, their common type becomes the handler's reply type. Conflicting reply paths
are compile-time errors, and an explicit `-> Type` remains an optional constraint.

Top-level unit tests and benchmarks use quoted names and ordinary indented Moss bodies:

```moss
test "addition":
  assertEqual(add(2, 3), 5)

bench "addition":
  add(2, 3)
```

They are project tooling declarations rather than runtime reflection or macros. Normal
application generation omits both. `assert` requires a boolean; `assertEqual` requires
compatible statically resolved values. See [unit testing](TESTING.md) and
[benchmarking](BENCHMARKING.md).

### Functional pipelines

Pipelines are typed expressions. The Phase 4 functional operations are `map`, `filter`,
`reduce`, `sum`, `count`, `any`, and `all`:

```moss
result = values
    |> map(normalize)
    |> filter(_ > 0)
    |> map(_.score())
    |> sum

total = values |> reduce(0, add)
```

`map` and `filter` are logically eager, ordered collection transformations. Each stage
observes source elements in order and logically completes its result collection before
the following stage begins. Both operations READ their input collection and produce a
new value; neither operation implicitly mutates or consumes the source. The compiler may
eliminate intermediate collections only when its callable/effect analysis proves that
element-at-a-time execution is observably equivalent to this eager meaning.

`sum`, `count`, `any`, `all`, and `reduce` are terminal operations. `reduce` requires an
explicit initial accumulator. On empty input, `sum` and `count` return zero, `any` returns
false, `all` returns true, and `reduce(initial, fn)` returns `initial`. Integer reductions
use Moss wrapping arithmetic. A nontrivial bound `initial` transfers into the reduction
and is unavailable afterward; the compiler never clones it to implement the empty path.
The initializer is evaluated once when the ordered reduction stage begins. Its inferred
effects participate in fusion legality, so an effectful or possibly failing initializer
keeps the eager stage schedule.

The eager reference meaning evaluates every `any`/`all` predicate in source order.
Under `-O`, a pure and non-failing terminal may stop once its result is known. Exact
`count` may lower to collection length, including through dead pure maps. These are
compiler optimizations, not lazy source semantics; observable or possibly failing
callbacks retain the complete eager traversal.

An immutable, single-use functional binding may be physically virtual when its sole
consumer is the immediately following pipeline in the same lexical block. Multiple
uses, mutation, effects, ownership boundaries, and control flow require a concrete
collection. Likewise, adjacent independent terminals over the same unchanged local
source may share one traversal. Neither transformation changes source syntax or permits
a compiler-internal pipeline value to escape.

Phase 4.6 performs bounded Moss-to-Moss semantic rewrites before ordinary lowering.
Adjacent proven-safe maps and filters may compose; safe trailing maps disappear when a
terminal `count` cannot observe their values; inert literal cardinality may become a
constant; and a filter may move through trivial `map(_)`, whose identity is statically
proven. General map/filter reordering is not implied. Any rewrite that skips callback
work requires that work to have no observable effect, failure, or conservative
`may_diverge` fact. This optimizer does not make Moss lazy and introduces no source
syntax.

A stage callable may be a named function, a statically bound instance method such as
`scaler.apply`, or a placeholder expression such as `_ > 0`, `_ * scale`, or
`_.score()`. A bound method captures one concrete receiver and must only READ it;
placeholder expressions may likewise read immutable surrounding locals. A higher-order
helper can accept an untyped callable parameter, but every call site must close that
parameter to one statically known function identity. Moss emits a
concrete specialization rather than a function object, function pointer, vtable, or
runtime lookup. General lambdas and dynamically escaping callable values are not part of
this source surface.

Pipeline values are compiler structure, not lazy runtime iterators. A transformation
pipeline whose result is still a collection cannot cross `message`, `await`, or `reply`
directly; bind its materialized result first. A terminal already produces a concrete
scalar and may cross an explicit domain copy boundary normally. All ordinary ownership
and alias checks also apply inside callbacks. A consuming callback is rejected when
current collection semantics cannot provide ownership without an implicit copy.
For the same reason, mapping `_` or a non-Copy projection such as `_.payload` out of a
READ-only source is rejected rather than cloned implicitly. Capture mutation is rejected
even when the WRITE/CONSUME requirement is propagated through an ordinary helper call.

### Integer arithmetic

`Int` is currently a signed 64-bit two's-complement value. Moss defines integer
arithmetic independently of the generated Rust build profile: an overflowing
result wraps modulo 2<sup>64</sup> and is interpreted again as a signed value.
Integer `+`, `-`, and `*` therefore wrap at the `i64` boundary. Integer `/`
likewise wraps the `Int`-minimum divided by `-1` case; division by zero remains
invalid.

This rule also applies to compiler-generated arithmetic representing the same
source operation, including integer `sum`, domain-state updates, and the new
value returned by an atomic add/sub handler. The Rust backend emits explicit
`i64` literals and wrapping operations; Rust debug overflow checks must not
change observable Moss behavior. Comparisons and boolean operations are not
changed by this rule.

## Domain communication

Cross-domain communication is always visible in source. The three forms have distinct
meaning:

```moss
foo()                              # ordinary local/intra-domain call
message Portfolio.apply_fill(fill) # asynchronous Moss domain message
position = await Portfolio.position(symbol) # Moss request and wait for its reply
```

`message` is fire-and-forget. `await` is specifically the Moss domain request/reply
operation; it is not general coroutine syntax. The compatibility form
`let position = await Portfolio.position(symbol)` remains accepted while assignment
syntax is migrated. The awaited destination's type is inferred from the handler's
declared or inferred reply type.

A naked dotted call whose receiver is a domain reference, such as
`Portfolio.apply_fill(fill)`, is a compile-time error requiring `message` or `await`.
The checker must distinguish ordinary local calls, asynchronous messages, and awaited
messages in the AST/IR so this rule does not depend on Rust code generation.

The existing Moss rules remain in force: domains own mutable state, handlers run to
completion without re-entrancy, sender-to-receiver message order is preserved, request /
reply handlers require an explicit reply, ownership boundaries are checked statically,
and await blocks the requesting domain according to the current runtime contract.

Every awaited receiver must resolve to one concrete domain type during ordinary static
checking, including awaits in helpers used only by `main`. Type environments fork at an
`if`/`else` and merge afterward. A binding created on both paths remains available only
when both paths assign the same concrete type; different domain types are rejected, and
a binding created on only one path is not definite after the join. Moss does not infer a
domain union from branch order. Await dependency discovery still visits every branch,
including a syntactically false branch.

## Rust boundary

Moss source does not expose lifetimes, borrow annotations, `Arc`, `Mutex`, `Send`,
`Sync`, or ownership boilerplate. Frontend inference and Moss semantic checks determine
the required properties. Generated Rust may use Rust's type and borrow checker as an
additional verifier. A semantically valid Moss program that fails generated Rust
checking is a compiler or backend defect to fix in the implementation.

The compiler may normalize new syntax into the existing internal statements and Rust
lowering while the frontend evolves. Such normalization is an implementation plan; it
must preserve the source meanings above and must not make backend mechanisms observable
in Moss.

With optimization enabled, the backend may implement the same domain as a mailbox, a
direct `Mutex`/`RwLock` state object, a set of `SeqCst` atomics, or a configured local
cluster. It may also batch adjacent sends or reuse a proven-exclusive lock guard. These
choices add no source category: `message`, `await`, and `reply` remain copy boundaries;
handlers remain serialized and non-reentrant, and sender FIFO remains a source-visible
guarantee. This text does not assert one universal domain commit order beyond those
guarantees. `-O0` is the ordinary mailbox reference lowering. Generated
comments expose the selected plan for testing, but Rust locks, atomics, queues, and
threads are not Moss semantics.

The global await DAG is required by both the source concurrency model and direct
shared-memory lowering. It prevents logical non-reentrant await deadlocks and cyclic
nested acquisition of domain state locks. A direct-shared handler currently keeps its
source state lock while awaiting a mailbox-backed domain, so the lock can remain held
for the target's full request/reply latency. This is semantically correct and recorded
as a future performance concern rather than changed by the current backend.

Phase 10.5 does not introduce self-send or same-domain handler-chaining syntax. Put
shared handler logic in ordinary statically resolved helpers. Historical backend notes
may mention queued self-messages; their final source-level status is intentionally
superseded/open rather than changed by this documentation checkpoint.

## Static iteration

`for` is the statically resolved iteration form. Its source is resolved at compile
time to a compiler-native traversal for `Vector` and `range`, or to a concrete
`Iterator` shape with `next() -> Option[element]`:

```moss
for value in values:
  process(value)

for index in range(0, 10):
  process(index)
```

`range(start, end)` is half-open (`start` is included and `end` is excluded) and
uses `Int`. A three-argument form with a statically known positive non-zero step is
also accepted. `while` remains the general arbitrary imperative loop. Moss does not
create runtime iterator objects, vtables, boxing, or dynamic iterator dispatch.

Ordinary collection traversal is READ traversal. Structural collection mutation,
such as `values.push(x)`, is rejected while that traversal is active. A user-defined
iterator may mutate its own concrete iterator state through its statically resolved
`next` method; that ownership effect is checked normally.

## Migration status and compatibility

Before the frontend migration, the compiler accepted `type Name = object`, `on` domain
handlers, `proc main()`, `let`/`var` declarations, and dotted message calls. The
migration adds the `fn`, `type Name:`, inferred state bindings, inferred handler replies,
`message`, assignment-await, pipeline, and `=` constructor forms incrementally. Existing
examples are written in the preferred syntax; compatibility tests retain older spellings
where useful. The old naked dotted spelling is rejected when its receiver is a domain
reference. Legacy declarations remain available where they do not make the communication
boundary ambiguous. The current parser accepts top-level functions and function-like
domain handlers, but does not yet support nested function declarations or general method
values.

The implementation must record any deviation from this document in the project
checkpoint and design records. Parser limitations are not new Moss semantics.
# Typing and compiler architecture

## Message payloads

Every incoming handler argument is an immutable value-copy snapshot. A handler
may read fields, call READ-only helpers, and forward the value through another
explicit `message` boundary. It may not mutate or consume the incoming value,
move it into domain state, reassign the incoming binding, or reply with that
same snapshot. This restriction applies to every payload type, including
primitive `Copy` values and domain handles. Reply with newly computed data
instead:

```moss
domain Processor:
  fn Size(payload: Record) -> Int:
    reply payload.size()
```

The restriction is checked through the normal ownership/effect analysis,
including calls to helpers and methods. A forwarding `message` is a new copy
boundary and therefore does not consume the original payload. `Copy` is only a
backend/property distinction: primitive values may continue to be passed by
value, but it does not weaken payload immutability or the no-original-reply
rule.

The mailbox backend materializes the independent snapshot. A synchronous
shared-memory backend may implement the same semantics by passing a temporary
immutable Rust reference for a nontrivial payload: the handler finishes before
the sender can mutate its value, and the reference cannot escape the call.
This is an implementation optimization, not a change to Moss semantics.

Moss variables and parameters are either untyped or typed. Untyped means
statically duck typed. Typed means annotated with either a concrete type or a
trait. Moss has no dynamic typing. All required type and operation relationships
are resolved and verified at compile time.

Generic and template variables may exist internally in the compiler, but they
are not part of normal Moss source syntax. Collection element, key, and value
types are inferred internally for `Vector`, `Map`, and `Queue`; source code does
not write `Vector[T]` or `Map[K, V]`.

Method requirements inferred from an untyped parameter retain the method name,
arity, argument relationships, and relevant result relationship. Each concrete
call site is verified with the ordinary concrete-method resolver. A named trait
uses that same resolver for every declared method. Method-constrained and
trait-typed local functions are emitted as concrete call-site specializations;
there is no runtime method search, trait object, vtable, or implicit `Any`.

The current implementation has begun separating semantic data (`src/ast.hpp`),
inferred requirements (`src/constraints.hpp`), and diagnostics
(`src/diagnostics.hpp`). The remaining parser, inference, ownership, domain,
optimization, and Rust lowering code is still being extracted incrementally.

## What Moss looks like

The executable showcase set in [`examples/`](../examples/) is the quickest syntax
tour. [`static_duck_typing.moss`](../examples/static_duck_typing.moss) demonstrates
method-based structural requirements; [`traits.moss`](../examples/traits.moss)
demonstrates two concrete implementations of one named trait;
[`collections_and_methods.moss`](../examples/collections_and_methods.moss) shows
inferred Vector, Map, and Queue element types; and
[`functional_dataflow.moss`](../examples/functional_dataflow.moss) executes a typed
`values |> map(...) |> filter(...) |> map(...) |> sum` pipeline that `-O` fuses into
one explicit loop when its effects are safe. The Phase 4.5 examples
[`functional_terminal_optimization.moss`](../examples/functional_terminal_optimization.moss),
[`functional_scope_fusion.moss`](../examples/functional_scope_fusion.moss),
[`functional_shared_traversal.moss`](../examples/functional_shared_traversal.moss), and
[`functional_materialization.moss`](../examples/functional_materialization.moss) show
terminal simplification, scope fusion, DAG traversal sharing, and explicit
materialization. The larger
[`mini_application.moss`](../examples/mini_application.moss) combines those static
features with domains, `message`, and `await`. These are ordinary source programs:
Moss resolves calls before Rust generation and does not create runtime trait objects.
## Modules

`module name` declares a logical module; multiple physical files may declare
the same module. `import name` introduces a qualified module name. Declarations
are private unless marked `export`:

```moss
module pricing
export fn notional(x: Int) -> Int:
  x * 2
```

Use `pricing.notional(value)` from an importing module. Typed exports are
materialized ABI. Untyped exported parameters are compile-time Moss generics,
shipped as semantic IR and specialized by concrete use. Existing projects
without explicit module declarations remain implicit single-module projects.
