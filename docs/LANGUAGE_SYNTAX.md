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
their serialized handler semantics; the first migration retains the existing `on`
handler spelling so a local function cannot be mistaken for a message endpoint.

Pipelines are expressions:

```moss
data
    |> parse
    |> normalize
    |> score
```

The frontend initially normalizes a pipeline to ordinary nested local calls. The source
form leaves room for future fusion, SIMD, and GPU planning without making those backend
choices language semantics.

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
syntax is migrated.

A naked dotted call whose receiver is a domain reference, such as
`Portfolio.apply_fill(fill)`, is a compile-time error requiring `message` or `await`.
The checker must distinguish ordinary local calls, asynchronous messages, and awaited
messages in the AST/IR so this rule does not depend on Rust code generation.

The existing Moss rules remain in force: domains own mutable state, handlers run to
completion without re-entrancy, sender-to-receiver message order is preserved, request /
reply handlers require an explicit reply, ownership boundaries are checked statically,
and await blocks the requesting domain according to the current runtime contract.

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

## Migration status and compatibility

Before the frontend migration, the compiler accepted `type Name = object`, `on` domain
handlers, `proc main()`, `let`/`var` declarations, and dotted message calls. The
migration adds the `fn`, `type Name:`, `message`, assignment-await, and pipeline forms
incrementally. Existing examples and tests are migrated to explicit `message` sends so
the naked cross-domain-call diagnostic can become authoritative. Legacy declarations
remain available where they do not make the communication boundary ambiguous.

The implementation must record any deviation from this document in the project
checkpoint and design records. Parser limitations are not new Moss semantics.
