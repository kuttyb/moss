---
name: moss-language
description: Write, review, or debug current Moss v0.1 source (.moss, Moss projects, modules, domains, messages, traits, ownership/effects, and functional pipelines). Use before editing Moss; do not infer Moss from Rust or historical fixtures.
metadata:
  language: moss-0.1
  skill-version: "1"
  validation: tests/tooling/check_agent_skills.py
---

# Moss language — current v0.1

Moss is a statically compiled, inference-driven language. Write ordinary source
with little type or synchronization ceremony; the compiler specializes and closes
the reachable program before native generation. Mutable shared state lives in
domains. Cross-domain work uses synchronous `message` calls. Programmers do not
write locks: the compiler derives effects, synchronization classes, and a safe
acquisition order before emitting safe Rust.

Prefer the current constructs below over guesses imported from Rust, actor
languages, or old Moss source. The compiler and its current regression suite are
authoritative. If this skill disagrees with a compiler diagnostic, trust the
compiler, keep the program semantically valid, and report the skill drift.

Before substantive Moss work, load `moss-agent-workflow` and run
`moss agent bootstrap --json`. This skill's `moss-0.1` contract must agree with the
running compiler's `result.language_version`; if it does not, use live bootstrap,
capabilities, and schema discovery before proceeding.

## Machine-checkable guidance contract

```yaml
moss_skill_contract:
  language_version: moss-0.1
  skill_version: 1
  message: synchronous-blocking
  await: retired
  spawn: retired
  domain_handles: routing-capabilities
  self_send: forbidden
  same_domain_message: forbidden
  locks: compiler-derived
  traits: structural-compile-time
  parameter_effects: inferred-read-write-consume
  recursion: unsupported
  general_first_class_closures: unsupported
  legacy_runtime_model: forbidden
  bootstrap_contract: required
  project_driver: margo
  project_manifest: Moss.toml
  project_lockfile: Moss.lock
  package_resolution: path-git
  module_resolution: moss-compiler
  source_surface: bootstrap-discoverable
  canonical_docs: bootstrap-discoverable
  project_surface: bootstrap-discoverable
  collection_operations: bootstrap-discoverable
  test_domain_topology: bootstrap-discoverable
  composition_initializers: bootstrap-discoverable
  domain_fn: handler
  explicit_let: immutable
  explicit_var: mutable
```

## Core source surface

Use live bootstrap's compact `source_surface` before guessing a spelling. For
fresh Moss source-writing or unfamiliar language work, bootstrap's
`canonical_docs.practical_language_guide` points to the Gentle Introduction;
read it when practical context is needed rather than treating every task as a
full-document reading assignment. The high-frequency current forms are:

```moss
x = expression        # usual inferred binding
let x = expression    # explicitly immutable local
var x = expression    # explicitly mutable local

if condition:
  ...
else:
  ...

while condition:
  ...

for item in expression:
  ...

for i in range(start, end):
  ...
for i in range(start, end, step):
  ...

if not ready:
  ...
```

`let` cannot be reassigned or used as the writable receiver of a mutating
operation; use `var` when mutation is intended. `Vector`, `Map`, and `Queue`
are the current built-in collections. Ordinary functions use `fn` and return
with `return`; `message` crosses domains. Every `fn` declared directly inside a
`domain` is a **handler**, so a value-returning handler uses `reply`, not an
ordinary helper `return`. Reusable implementation logic belongs in an ordinary
non-domain function.

### Arithmetic and empty collections

Current integer arithmetic uses `+`, `-`, `*`, `/`, and `%`:

```moss
remainder = value % capacity
```

`%` is integer remainder and follows Moss's truncating integer-division
semantics; it is not Python floor-modulo. Use a literal when elements establish
their type, and the typed built-in constructor when an empty vector needs one:

```moss
values = [1, 2, 3]
var pending = Vector[Int]()
```

Moss has no user-defined/source generic type variables or templates. Concrete
built-in collection types such as `Vector[Int]`, `Map[String, Int]`, and
`Queue[Int]` may appear in concrete type positions; `Vector[T]()` is a built-in
typed empty-vector constructor, not general generic nominal construction. Local
type annotations such as `var values: Vector[Int] = []` are not supported in
v0.1; use inferred locals and `Vector[Int]()` instead.

```moss
fn adjusted(value: Int) -> Int:
  return value + 1

domain Counter:
  value = 0

  fn Increment() -> Int:
    value = adjusted(value)
    reply value
```

An ownership diagnostic describes the access requirements of the attempted
expression. It does not by itself prove that the surrounding data structure or
architecture is unsupported. Before substantially restructuring an approach,
reduce it and ask `moss ownership`, `moss effects`, and `moss check
<minimal-reproducer> --json`. For example, one `Vector[Task]` ownership error
does not establish that Moss requires primitive IDs instead of objects.
When `OWNERSHIP_CONFLICTING_ACCESS` supplies structured argument entities, use their
compiler-inferred modes and expressions directly: overlap is legal only when every
reported mode is `READ`. Its `separate-conflicting-access` guidance never implies an
automatic copy.

For a typed `Map[K, V]`, strict indexing is `map[key]`; defaulted lookup is
`map.get(key, default)`. `map.keys()` and `map.values()` return eager owned
`Vector` snapshots and have unspecified order. They are not borrowed iterator
views, and bare `Map` is not itself a `for` source.

### Collection operations reference

- `Vector`: `vec.push(item)`, `vec.pop()`, indexed `vec[i]`, and `vec[i] = item`. Cardinality is `vec |> count` or manual tracking. Operations like `.len()`, `.size()`, `.remove()`, or `.clear()` do not exist.
- `Map`: strict indexing `map[key]`, indexed assignment `map[key] = value`, and defaulted lookup `map.get(key, default)`. `map.keys()` and `map.values()` return eager owned `Vector` snapshots. Deletion/removal operations (`.remove()`, `.delete()`, `.clear()`) do not exist in v0.1.
- `Queue`: `queue.push(item)` and `queue.pop()`.

For full examples, see `docs/GENTLE_INTRODUCTION_TO_MOSS.md`.

## Tests, topology, and composition initializers

The test form is `test "name":`, with `assert(condition)` and
`assertEqual(actual, expected)`. A test block is not a separate domain
composition root: concrete domain instances belong to `main`'s initial static
composition prefix. Test files participate in the appropriate logical test
compilation unit, so they can verify behavior of that one composed topology;
see `docs/TESTING.md` for test-project details.

Domain construction uses direct named member bindings in that prefix. A domain
state initializer must be side-effect-free: messages, domain access, I/O,
failing, divergent, and unresolved work are rejected. “Unresolved” means the
compiler cannot statically establish an expression or call's relevant observable
effects; it does not mean locals, ordinary local computation, normal allocation,
or a multi-statement pure helper. The current checker does
accept a pure ordinary helper call in such an initializer, so do not mistake the
restriction for a ban on every helper. Use `source_surface.domains.composition`
for the compact live rule, then reduce an unfamiliar initializer to a minimal
`moss check --json` probe.

## Do / do not

Do:

- Construct every domain statically in the initial composition prefix of `main`.
- Keep domain state initializers side-effect-free; a pure ordinary helper is
  permitted, but a constructor initializer cannot perform message, domain, I/O,
  failing, divergent, or unresolved work.
- Declare a domain's outbound dependencies with `domainroutes(...)`, then bind
  those named routes at construction.
- Use `message target.Handler(args...)` for every cross-domain handler call.
- Use `reply value` to terminate a value-returning handler.
- Put reuse within one domain in an ordinary helper function; calls are statically
  resolved.
- Let the compiler infer `READ`, `WRITE`, and `CONSUME` parameter effects. Make
  a separate local binding when only scratch mutation is intended.
- Use structural traits and concrete operations rather than nominal declarations.
- Use named functions for nontrivial pipeline logic. Tiny supported placeholder
  expressions are appropriate for small pipeline stages.
- Keep domain handles in the static routing topology and ask the compiler for
  semantic facts when unsure.

Do not write or simulate:

- `await`: it is retired. Use synchronous `message` directly.
- `spawn`: it is retired. Construct a domain in `main`'s composition prefix.
- Lock, mutex, or RwLock syntax: there is no source-level lock programming model.
- `message self.X(...)`: self-send is illegal. Extract ordinary helper logic.
- A handler-to-handler `message` on the same domain: use an ordinary helper.
- A domain handle as a function or handler argument, payload, reply, local alias,
  state value, or collection element: declare a `domainroutes` dependency instead.
- Domain construction in a loop, branch, helper, handler, or collection.
- Runtime/dynamic dispatch, nominal `implements Trait`, user-written `mut`,
  `inout`, or `write` parameter annotations, Rust-style generic type variables,
  general first-class closures, or ordinary recursion.

## Retired syntax

`await` and `spawn` are both retired. Migration is a direct syntax change, not an
alternate execution model:

```moss
# Old — rejected
result = await worker.Get()

# Current
result = message worker.Get()
```

```moss
# Old — rejected
worker = spawn Worker()

# Current, in main's initial composition prefix
worker = Worker()
```

Do not retain either spelling in new source. A current compiler diagnostic points
to the corresponding replacement.

## Domains, routes, messages, and replies

A domain owns mutable shared state. Treat it as a coarse architectural subsystem,
not one domain per record, user, or object. Concrete instances are fixed by the
initial `main` composition prefix. A `domainroutes` declaration supplies immutable
outbound route slots; state names and route names share one member namespace.
The compiler turns all bound routes into a closed concrete topology, so no ordinary
value operation may create, copy, or store a routing capability.

`message` is synchronous and blocking. It can be used for a value or as a statement:

```moss
current = message database.Lookup(key)
message logger.Record(current)
```

The caller continues only after the target handler terminates. A handler declared
with a value result must `reply` on every normal path. `reply expr` establishes a
new semantic value boundary and terminates that handler immediately. A no-value
handler may complete normally.

Incoming handler arguments are immutable value snapshots. A handler may read an
incoming payload, pass it to a READ-only helper, use it to compute data, forward it
through another `message`, or reply with it by value. It may not write, consume,
rebind, or move the payload into state. This is a Moss semantic rule for every
payload type; whether a backend representation is cheap to copy does not relax it.

There is no self-send and no same-domain handler chaining. Put shared handler
implementation in an ordinary helper with normal lexical/module scope. The helper
may operate on compiler-approved state access; it is not another domain boundary.
The compiler distinguishes `DOMAIN_SELF_MESSAGE` / `DOMAIN_SAME_INSTANCE_MESSAGE`
(`local-helper`) from a real cross-domain `DOMAIN_HANDLER_REQUIRES_MESSAGE`
(`use-message`) and from missing static route declaration/binding diagnostics. Do not
apply one repair across those different checked causes.
Handler-specific guidance is emitted only after the handler resolves. In structured
diagnostics, a cause entity's `semantic_identity` is the actual compiler identity or
`null`; a canonical selector such as `handler:Store.Read` belongs in `name`.

### Complete current example

The following listing is intentionally mirrored by
`tests/tooling/fixtures/moss_language_skill_example.moss` and is compiled by the
skill drift test.

<!-- moss-skill-valid-example:start -->
```moss
fn add(left, right):
  return left + right

domain Audit:
  total = 0

  fn Record(amount: Int):
    total = add(total, amount)

domain Account:
  balance = 0

  domainroutes(audit: Audit)

  fn Deposit(amount: Int) -> Int:
    balance = add(balance, amount)
    message audit.Record(amount)
    reply balance

fn main():
  audit = Audit()
  account = Account(audit: audit)
  value = message account.Deposit(5)
  echo value
```
<!-- moss-skill-valid-example:end -->

`Account` has one declared static route to `Audit`; `Deposit` blocks until
`Record` completes. `add` is an ordinary helper. Its parameter effects are inferred
from its body and use, while the state writes are ordinary domain-state mutation.
No source lock or routing capability is passed as data.

## Ownership and inferred effects

Moss tracks access modes as `READ`, `WRITE`, and `CONSUME`. They are compiler facts,
not parameter modifiers a programmer writes. For example, assigning a primitive
parameter can infer a caller-visible WRITE:

```moss
fn increment(value):
  value = value + 1
```

When only local scratch mutation is intended, create local storage instead:

```moss
fn inspect(value):
  current = value
  current = current + 1
  return current
```

An rvalue cannot satisfy a WRITE or CONSUME requirement. When two arguments overlap
the same storage, the allowed combinations are `READ + READ` only. `READ + WRITE`,
`READ + CONSUME`, `WRITE + WRITE`, `WRITE + CONSUME`, and `CONSUME + CONSUME` are
rejected. Do not work around these diagnostics with invented copies or annotations;
restructure storage, calls, or intent explicitly.

Moss does not make arbitrary implicit deep copies. Cross-domain `message` arguments
and `reply` results are explicit semantic value boundaries. Internally protected
domain reads are compiler-managed borrows, not hidden snapshots, and such borrows
cannot escape through a message or reply.

## Traits, specialization, and calls

Traits are structural compile-time predicates, not nominal memberships. Do not write
`implements Printable`. Define the operations a trait requires; a concrete type
satisfies the trait when its statically specialized structure supplies those
operations. This does not create a runtime trait object, vtable, or dynamic dispatch.

Moss follows: **infer all the way, then close statically**. Untyped parameters are
statically duck typed, never runtime dynamic values. Before native generation the
compiler resolves all reachable concrete specializations, call targets, methods, and
structural requirements. Prefer inference and concrete use sites over Rust-style
generic type-variable ceremony. Ordinary recursion is not supported in v0.1; rewrite
the algorithm iteratively or report the current language limitation rather than adding
recursive source.

## Functional/dataflow code

The currently supported pipeline operations are `map`, `filter`, `reduce`, `sum`,
`count`, `any`, and `all`. Use named functions for substantial stage logic. Supported
small placeholders include forms such as `_ > 0`, `_ * scale`, and `_.score()`.
Captured values participate in effect inference; a capture that would write or consume
is not a loophole. A higher-order helper is allowed only when its callable parameter
specializes to a statically known target. General lambda values, escaping closures,
and dynamic callable dispatch are not Moss v0.1 features.

Pipelines are eager source semantics. The optimizer may fuse or avoid intermediate
work only when inferred effects, failure, and divergence facts prove it equivalent.
An observable `message` is an effect boundary: do not assume a transformation can move
across it. Use compiler `why` output rather than guessing why a pipeline did or did
not fuse.
Callable diagnostics distinguish an unsupported inline element spelling (use the `_`
placeholder), an invoked function in callable position (pass its name without
parentheses), and a callback that writes captured state (return a transformed value
from a pure callback). These are separate rules, not one generic pipeline failure.

## Modules and projects

A package root has `Moss.toml`, an optional deterministic `Moss.lock`, and
conventional `src/`, `tests/`, and `benches/` directories. A minimal `Moss.toml` is:

```toml
[project]
name = "my-project"
version = "0.1.0"

[build]
source = "src"
```

**Margo resolves packages; Moss resolves modules.** Use Margo for the package graph and project
commands; Moss remains the parser, checker, `.mossi` interface loader, and semantic
authority. Package dependencies are package roots, never individual `.moss` files:

```toml
[dependencies]
geometry = { path = "../geometry" }
math = { git = "https://github.com/example/math", tag = "v1" }
```

Git selectors resolve to concrete commits recorded in `Moss.lock`; Margo reuses its
global source cache at `${MARGO_HOME:-~/.margo}`. This is enough package context for
source work—see `docs/PROJECT_WORKFLOW.md` for operational details.

Without explicit modules, a target's participating files form one temporary global
compilation unit. Explicit modules use current syntax:

```moss
module pricing

export fn notional(value: Int) -> Int:
  value * 2
```

```moss
module app
import pricing

fn main():
  echo pricing.notional(21)
```

Declarations are private unless exported. Imports are acyclic and symbols are
qualified. The normal project entry is one application `main`. Compiled providers may
be consumed through their `.mossi` interface and paired native artifact when Margo
makes them source-free; Fast Debug requires reachable Moss source. A requested
external module must have exactly one `.mossi` provider in Margo's resolved dependency
environment. Two reachable packages may both export `Utils` until source writes
`import Utils`; that import then fails with `MODULE_IMPORT_AMBIGUOUS`. Moss source has
no package-qualified import syntax: Margo chooses reachable packages, then Moss
resolves imports among their module interfaces. See `docs/MODULES.md` and
`docs/PROJECT_WORKFLOW.md` for deeper details.

## Synchronization mental model

Write domain state, route declarations, and handlers. The compiler infers
READ/WRITE/CONSUME effects, derives synchronization classes and ordered acquisition,
then emits direct Rust locking. Conflicting handlers in one domain are synchronized;
unrelated domains are not a promise of one global total order. A synchronous message
creates program order between its caller and completed handler.

Never attempt to solve a synchronization diagnostic by adding a lock. Moss has no lock
syntax. Restructure ownership, a route, a domain boundary, or the handler's effects.

## Fast checklist before writing Moss

1. Is mutable shared state inside an appropriately coarse domain?
2. Are all domain instances and routes statically constructed in `main`?
3. Is every cross-domain operation a synchronous `message` through a declared route?
4. Is same-domain reuse an ordinary helper?
5. Are payloads ordinary values, never domain handles?
6. Are parameter and alias effects allowed by the inferred ownership contract?
7. Did you avoid retired syntax, locks, nominal traits, recursion, and invented
   dynamic/generic/closure features?

For live facts, run `moss agent bootstrap --json`, then use structured diagnostics
and semantic queries. The companion `moss-agent-workflow` skill specifies that loop.
