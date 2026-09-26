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

Moss uses significant indentation. A file uses one indentation width throughout,
set by its first indented line; `moss fmt` canonicalizes source to two spaces per
block level, and the examples here use that form. Tabs are rejected. Comments begin
with `#` outside a string. A trailing colon on a block header is optional and does
not change the block's meaning, except that the `type Name:` declaration requires
it; examples always write the colon. The frontend may retain compatibility spellings
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
  sqrt(dx * dx + dy * dy)
```

The same function may omit annotations when its complete meaning is inferable:

```moss
fn distance(a, b):
  dx = a.x - b.x
  dy = a.y - b.y
  sqrt(dx * dx + dy * dy)
```

Inference is static and whole-program where necessary. Literal types, operators,
field projections, constructors, function results, and declared domain handlers provide
constraints. An unresolved parameter, field, return type, or call is a compile-time
error. A failed inference must produce a Moss diagnostic rather than a Rust dynamic
fallback.

The initial migration supports the existing primitive types and value-object types.
Inference will grow incrementally; a construct is accepted only when the compiler can
materialize a concrete type for the generated Rust representation. In particular,
parameters of methods declared inside a `type` must currently be annotated; the checker
does not yet infer them from call sites.

### Local bindings

The usual binding form is inferred:

```moss
x = expression
```

Moss also has explicit local mutability forms:

```moss
let name = "moss"
var count = 0
count = count + 1
```

`let` is explicitly immutable and cannot be reassigned or used as the writable
receiver of a mutating operation. `var` is explicitly mutable. Local type
annotations such as `var values: Vector[Int] = []` are not part of v0.1;
ordinary locals are inferred instead.

A name first bound inside a `for` or `while` body, including a `for` loop
variable, is local to that loop: the loop may run zero times, so the name is not
visible after it. A name first bound inside an `if` is visible after the `if`
only when every branch, `if` and `else` alike, binds it; a branch-only binding
stays inside its branch. Updating a name that already exists outside the loop or
branch is ordinary mutation and remains visible afterward. When a confined local
shadows a module function of the same name, the name refers to that function
again after the construct. Referring to a confined name after its construct is a
compile-time error, usually `unknown identifier`:

```moss
var total = 0
for value in values:
  doubled = value * 2
  total = total + doubled
# total is visible here; value and doubled are not

if ready:
  label = "go"
else:
  label = "wait"
# label is visible here because both branches bind it
```

## Value types

The preferred type declaration is an indented block:

```moss
type Quote:
  symbol: String
  price: Float
  size: Int
  ts: Int
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

A state field whose type is not determined by an annotation, an initializer, or other
static constraints is a compile-time error. An annotated field may omit its initializer
and be bound by name at construction, as in `Counter(value = 0)`. The compatibility
spelling `var value: int = 0` remains accepted, but new source should use the binding
form above.

Named object construction uses `=` for value bindings:

```moss
Quote(symbol = "MOSS", price = 12.5, size = 100, ts = 1)
```

The older `Quote(symbol: "MOSS", price: 12.5, size: 100, ts: 1)` form remains accepted
during migration.
The colon continues to mark type constraints in declarations and parameters.

## Functions and expressions

Top-level local functions use `fn` and can be written as an expression or an indented
block:

```moss
fn square(x) = x * x

fn normalize(value, total):
  scaled = value * 100
  scaled / total
```

The final expression of a block is its result. Explicit `return value` may be retained
for migration and early exits. A call such as `normalize(value, total)` is an ordinary
local, intra-domain call. It has no mailbox, request/reply, or domain scheduling meaning.

`fn main()` is the preferred spelling for the program entry point; `proc main()` remains
a compatibility spelling while existing programs migrate. A `fn` declared directly
inside a `domain` is a domain message handler; `fn Handler(...)` is the canonical
spelling. Legacy `on Handler(...)` may remain accepted for migration compatibility.
Handlers are entered only through synchronous `message`; the declaration does not imply
that all handlers on one domain are globally serialized. The production compiler may
allow compatible executions to overlap under compiler-derived synchronization.
A top-level `fn` declares an ordinary local function. Domain and handler headers may carry a trailing `:` when using indentation
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

### Operators are closed, not overloadable

Moss v0.1 has a closed compiler-defined operator set. A user-defined `type`
cannot declare, overload, or replace the meaning of an operator. Accepted
operator/type combinations are language rules rather than methods discovered on
the operand types.

`String` supports built-in `+` concatenation and `==`/`!=` equality.
Lexicographic ordering is intentionally not a v0.1 operator surface:
`String < String`, `<=`, `>`, and `>=` are rejected by the frontend.

Binary operators outside the closed set are rejected by the frontend rather than
passed through to generated Rust. v0.1 has no bitwise `&`, `|`, or `^`, no shifts
`<<` or `>>`, and no symbolic `&&` or `||`; Boolean logic uses the word operators
below.

### Boolean operators

Moss uses the word operators `not`, `and`, `xor`, and `or`. They are
statically Bool-only; Moss does not use Python's operand-returning truthiness
semantics. `and` and `or` short-circuit, while `xor` evaluates both operands
and returns true exactly when one operand is true. Precedence from highest to
lowest is:

```text
not
and
xor
or
```

Parentheses may be used to override this order.

### Built-in collections

The current built-in collection surface is intentionally small:

```text
Vector:  push(item), pop(), vec[i], vec[i] = item
Map:     map[key], map[key] = value, get(key, default), keys(), values()
Queue:   push(item), pop()
```

`Map.get(key, default)` is the safe/defaulted lookup; `map[key]` is strict.
`keys()` and `values()` return eager owned `Vector` snapshots in unspecified
order. Cardinality is available through the `count` pipeline terminal.

### Functional pipelines

Pipelines are typed expressions. The Phase 4 functional operations are `map`, `filter`,
`reduce`, `sum`, `count`, `any`, and `all`:

```moss
result = values
  |> map(normalize)
  |> filter(_ > 0)
  |> map(_ * scale)
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
placeholder expressions may likewise read immutable surrounding locals.

An ordinary function can accept an untyped callable parameter and use it as a pipeline
stage; since Phase 15.13 it can also invoke the parameter directly:

```moss
fn apply(operation, value: Int) -> Int:
  operation(value)

fn total_by(values: Vector[Int], f) -> Int:
  values |> map(f) |> sum
```

Every call site must close the parameter to one statically known named function, as in
`apply(double, 5)`; an unqualified sibling function and its module-qualified spelling
name the same function. Moss emits one concrete specialization per function identity
rather than a function object, function pointer, vtable, or runtime lookup. The passed
function currently needs annotated parameter types, as a pipeline stage does. A
callable parameter cannot be returned, stored in a field or collection, or passed
across `message` or `reply`, and forwarding it into another helper's callable parameter
is not currently supported. Placeholder expressions and bound methods such as
`scaler.apply` remain pipeline-stage forms, not callable arguments. General lambdas and
dynamically escaping callable values are not part of this source surface.

Pipeline values are compiler structure, not lazy runtime iterators. A transformation
pipeline whose result is still a collection cannot cross `message` or `reply`
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

`%` is integer remainder using the same truncating signed-division model as
`/`. Remainder by zero is invalid.

This rule also applies to compiler-generated arithmetic representing the same
source operation, including integer `sum` and domain-state updates. The Rust
backend emits explicit `i64` literals and wrapping operations; Rust debug overflow
checks must not change observable Moss behavior. Comparisons and boolean operations
are not changed by this rule.

## Domain communication

Cross-domain communication is always visible in source. Ordinary calls remain local;
`message` is a synchronous, blocking domain-handler invocation. Its receiver is a
concrete composition binding in `main` or a declared `domainroutes` slot, never a
domain type name:

```moss
foo()  # ordinary local/intra-domain call
position = message portfolio.position(symbol)
message portfolio.record(position)  # statement result is discarded
```

The caller resumes only after the handler terminates. `reply expr` establishes the
handler's value and terminates it immediately. A value-returning handler must reply on
every normal path; a no-value handler may complete normally. Source-level `await` is
retired and rejected with a migration diagnostic directing users to `message`.

A naked dotted call whose receiver is a domain reference is a compile-time error. Self-
send and same-domain handler chaining are also errors; reusable handler logic belongs in
an ordinary statically resolved helper. Domains own mutable state, handlers run to
completion without re-entrancy, and ownership boundaries are checked statically.

Message arguments and replies remain semantic by-value boundaries. Incoming payload
bindings are immutable snapshots: they may be read or forwarded, and the original
incoming value may be replied by value, but it may not be written, consumed, rebound,
or moved into state. This applies to primitive and nontrivial values alike.

Domains are constructed directly in the initial composition prefix of `main`:

```moss
ledger = Ledger()
app = App(ledger: ledger)
message app.run()
```

Domain dependencies are declaration-only route slots. State fields and routes share one
member namespace:

```moss
domain App:
  domainroutes(ledger: Ledger)
```

`spawn` is retired and reports a migration diagnostic. Construction is statically
enumerable from `main`; route topology is a concrete whole-program DAG with deterministic
unique `domain_rank` values. Synchronization classes, ranks, and acquisition placement
are compiler-owned facts rather than source syntax; Moss exposes no lock API.

State initializers may compute ordinary runtime values, including through pure helper
calls, but must be side-effect-free. The rule covers defaults written in the domain
declaration and constructor arguments in `main` alike. Messages, domain access, I/O,
possibly failing work (for example division, indexing, or `pop`), possibly divergent
loops, and unresolved calls are rejected. A helper `while` loop counts as terminating
only when the checker can prove it bounded. The accepted shape is a counting loop:

- The condition compares the counter, written on the left, against a bound with `<`,
  `<=`, `>`, or `>=`.
- The bound is invariant and side-effect-free, does not depend on the counter, and is
  not modified in the body.
- The body steps the counter toward the bound exactly once per iteration,
  unconditionally, by a positive integer literal.
- The loop contains no nested `while`.

`for` traversal of a `Vector` is accepted as bounded. `moss effects` reports the same
`may_fail`, `may_diverge`, and `unresolved` facts the initializer check uses.

### Closed routing capabilities (Phase 10.6B.1)

A domain handle is not an ordinary Moss value. Its only source-level roles are
a concrete binding in `main`'s initial composition prefix, the target of a named
`domainroutes` binding, and the receiver of `message`. Route slots cannot be
shadowed or reassigned.

Handles cannot be function/method/handler parameters, message payloads, reply
results, ordinary state fields, aggregate/collection elements, or local aliases.
This includes untyped helpers and nested container types. Use ordinary data for
payloads and declare dependencies structurally:

```moss
domain Worker:
  fn Run(value: Int) -> Int:
    reply value + 1

domain Manager:
  domainroutes(worker: Worker)

  fn Run(value: Int) -> Int:
    reply message worker.Run(value)

fn main():
  worker = Worker()
  manager = Manager(worker: worker)
  result = message manager.Run(41)
  echo result
```

Every cross-domain target is attributable to a declared concrete route edge.
Calls from `main` use its concrete composition bindings; they do not create
additional domain-to-domain edges. Multiple slots may target the same instance.
Ordinary execution cannot introduce another instance or routing capability.

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

Every production optimization level uses compiler-derived handler-level 2PL.
The compiler determines each handler's shared/exclusive synchronization classes
and their acquisition order. Eligible leading conditionals may defer branch-only
acquisitions or cancel a rank-forced guard that is proven untouched on the
realized path. Once touched, a guard remains held through completion; guards
backing borrowed message views remain held for the complete nested call.
Ordinary READs borrow protected stored values; message/reply remain independent
semantic value boundaries. Fast Debug directly interprets the same checked
synchronous semantics without simulated locks or scheduling.

Self-send and same-domain handler chaining are rejected. Put shared handler
logic in ordinary statically resolved helpers; earlier queued self-message designs
are historical and superseded.

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
uses `Int`. A three-argument form whose step is a positive non-zero `Int` literal is
also accepted. `while` remains the general arbitrary imperative loop. Moss does not
create runtime iterator objects, vtables, boxing, or dynamic iterator dispatch.
`Map` and `Queue` are not `for` sources; traverse a map through its `keys()` or
`values()` snapshot. The loop variable and any name first bound in the body are
local to the loop (see [Local bindings](#local-bindings)).

Ordinary collection traversal is READ traversal. Structural collection mutation,
such as `values.push(x)`, is rejected while that traversal is active. A user-defined
iterator may mutate its own concrete iterator state through its statically resolved
`next` method; that ownership effect is checked normally.

## Migration status and compatibility

Before the frontend migration, the compiler accepted `type Name = object`, `on` domain
handlers, `proc main()`, and dotted message calls. The migration adds the canonical
`fn`, `type Name:`, inferred state bindings, inferred handler replies, explicit
`message`, pipeline, and `=` constructor forms incrementally. `let` and `var` are
current supported local-binding forms: `let` is explicitly immutable and `var`
explicitly mutable. Most examples use the preferred syntax, but several older
functional examples still use the `proc main()` compatibility spelling, and
compatibility tests retain older spellings where useful. The old naked dotted spelling
is rejected when its receiver is a domain reference. Legacy declarations remain
available where they do not make the communication boundary ambiguous. The current
parser accepts top-level functions and function-like domain handlers, but does not yet
support nested function declarations or general method values.

The implementation must record any deviation from this document in the project
checkpoint and design records. Parser limitations are not new Moss semantics.

## Typing and compiler architecture

### Message payloads

Every incoming handler argument is an immutable value-copy snapshot. A handler
may read fields, call READ-only helpers, and forward the value through another
explicit `message` boundary. It may not mutate or consume the incoming value,
move it into domain state, or reassign the incoming binding. A reply is a new
semantic value boundary, so replying with the incoming value by value is legal.
This restriction applies to every ordinary payload type, including primitive
`Copy` values. Assigning a primitive payload such as an `Int` into state copies it
and is only a READ of the payload. A nontrivial payload, such as a `String`, a record,
or a collection, cannot be stored directly; store a new value built from it, for example
one returned by a helper. Domain handles are not payload values and cannot cross either
boundary. Derived replies remain ordinary newly computed data:

```moss
domain Processor:
  fn Size(payload: Record) -> Int:
    reply payload.size()
```

The restriction is checked through the normal ownership/effect analysis,
including calls to helpers and methods. A forwarding `message` is a new copy
boundary and therefore does not consume the original payload. `Copy` is only a
backend/property distinction: primitive values may continue to be passed by
value, but it does not weaken payload immutability.

`message` and `reply` remain **semantic** by-value boundaries. The production backend
materializes an owned reply and materializes an owned message payload at exported/native
or other ownership boundaries. For an internal synchronous `_shared` message, it may
instead lend a stable immutable Rust value or static state view for the duration of the
complete nested call. That temporary borrow is observationally equivalent to the Moss
snapshot: the receiver is READ-only, it cannot escape, and retained class guards keep
all reachable state leaves stable until the call returns. Ordinary protected READs use
the same view machinery; no Rust reference can escape through a `message` or `reply`.

### Typing

Moss variables and parameters are either untyped or typed. Untyped means
statically duck typed. Typed means annotated with either a concrete type or a
trait. Moss has no dynamic typing. All required type and operation relationships
are resolved and verified at compile time.

Generic and template variables may exist internally in the compiler, but they
are not part of normal Moss source syntax. Moss has no user-defined/source
generic type variables or templates in v0.1. Concrete applications of the
built-in collection types may appear where a concrete type is needed, such as
fields, parameters, and returns: `Vector[Int]`, `Map[String, Int]`, and
`Queue[Int]`. Ordinary locals are normally inferred. `Vector[T]()` is the
special built-in typed empty-vector constructor; local declaration annotations
such as `var values: Vector[Int] = []` remain unsupported.

Method requirements inferred from an untyped parameter retain the method name,
arity, argument relationships, and relevant result relationship. Each concrete
call site is verified with the ordinary concrete-method resolver. A named trait
uses that same resolver for every declared method. Method-constrained and
trait-typed local functions are emitted as concrete call-site specializations;
there is no runtime method search, trait object, vtable, or implicit `Any`.
Direct field access through an untyped parameter or an alias of it is rejected
at the field expression. Annotate the parameter with a concrete type to use its
fields, or expose shared behavior through a method. Untyped method calls retain
their inferred, unnamed static requirement; fields do not acquire one.
Consequently a trait is not a storage type: `Vector[Shape]()`, nested forms such as
`Vector[Vector[Shape]]`, and trait-typed collection fields are rejected. Keep each
concrete type in its own collection.

Indexing an untyped parameter with a built-in collection operation also
specializes at each concrete call. `fn first(items): return items[0]` accepts
both `Vector[Int]` and `Vector[String]` callers and returns the corresponding
element type. For `items[key]`, the concrete Vector, Queue, or Map constrains
the key; indexed assignment additionally constrains the assigned value and
infers a WRITE effect. A non-indexable argument or incompatible key/value is
rejected by Moss. These static checks do not prove bounds or Map key presence.
User-defined indexing remains a separate proposed feature (EXPRESS-008).

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
features with domains and synchronous `message`. These are ordinary source programs:
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
shipped as semantic IR and specialized by concrete use. The checked
specialization is projected into the module artifact containing that concrete
call: a provider wrapper carries the specializations it invokes, while a
source-free consumer may instantiate exported generic IR from `.mossi` in its
own artifact and link the provider rlib. No runtime dispatch or graph-wide
specialization crate is introduced. Existing projects without explicit module
declarations remain implicit single-module projects.
