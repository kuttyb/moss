# Functional/dataflow design and lowering

Phase 4 keeps functional intent as a typed Moss compiler concept long enough to analyze
and optimize it. It does not encode pipelines as Rust iterator chains or introduce lazy
runtime iterator objects.

```text
Moss source
    -> typed semantic representation
    -> ownership and callable/effect analysis
    -> functional/dataflow IR
    -> Moss-level optimization
    -> explicit loop/control-flow lowering
    -> safe Rust
```

## Source contract

The implemented operations are `map`, `filter`, `reduce`, `sum`, `count`, `any`, and
`all`. `map` and `filter` are transformations; the others are terminals. General
reduction always supplies an initial accumulator:

```moss
result = values
  |> map(normalize)
  |> filter(_ > 0)
  |> map(score)
  |> sum

total = values |> reduce(0, add)
```

The reference semantics are logically eager and ordered. The first `map` visits all
source elements in order and logically produces its result collection, then `filter`
does the same, and so on. A compiler optimization may remove those logical collections
only after proving that the changed execution schedule has the same result, effects, and
failure order **and termination behavior**.

Ordinary `map` and `filter` READ their source and produce a new Moss value. They are not
secretly destructive, and the source remains usable. On empty input:

| Terminal | Result |
| --- | --- |
| `sum` | numeric zero |
| `count` | `0` |
| `any(predicate)` | `false` |
| `all(predicate)` | `true` |
| `reduce(initial, fn)` | `initial` |

Integer terminals use the language's signed 64-bit wrapping arithmetic. For a nontrivial
bound accumulator, `reduce` transfers `initial` into its result. The old binding is
unavailable afterward, including when the input is empty; no copy is inserted.
The initializer is evaluated once at the ordered start of the reduction stage. Its
transitively inferred observable effects are merged into the `Reduce` IR node. An
effectful or possibly failing initializer therefore blocks stage fusion, preserving the
eager evaluation order under both `-O0` and `-O`.

## Static callables and typing

A stage accepts a named function, a statically bound instance method, or a placeholder
expression:

```moss
values |> map(normalize)
values |> map(scaler.apply)
values |> filter(_ > 0)
values |> map(_.score())
values |> map(_ * scale)
```

The placeholder is bound to the current element. It may read immutable surrounding
locals. A bound method captures one concrete receiver, which must be READ-only. Method
calls and named functions use the normal Moss static method/function resolver. Every
stage propagates concrete types: collection element to callable input,
callable output to the next stage, predicate result to `Bool`, and accumulator/result
relationships through `reduce`. Invalid relationships are Moss diagnostics before Rust
generation.

An untyped higher-order helper can carry a callable structurally:

```moss
fn transform(values, callable):
  return values |> map(callable)

result = transform(values, normalize)
```

Each call site must close `callable` to one known function identity. Moss records that
specialization dependency and emits a concrete helper with no runtime callable argument.
Unbounded callable identity is rejected; there are no boxed functions, vtables, dynamic
lookup, or general function-pointer escape.

After specialization, effect propagation is a first-order graph: a pipeline node points
to its concrete function/method specialization and then to ordinary local call edges.
Generic higher-order declarations may be deferred until a concrete call site, but an
unresolved indirect callable cannot enter checked functional IR. Captures are included
in the same summaries; a captured domain-state read is a domain READ effect. `message`
is an observable effect barrier, just like domain-state access, I/O, possible
failure, or potential divergence.

## Ownership and boundaries

Functional analysis consumes the Phase 2 READ/WRITE/CONSUME summaries rather than
bypassing them. A source collection is normally READ. Callback arguments and captures
retain their inferred effects and alias checks. Moss does not clone elements to satisfy a
CONSUME requirement. If current container ownership cannot make such an operation safe,
the program is rejected.

That rule also applies to placeholder results. `objects |> map(_)` and a projection such
as `objects |> map(_.payload)` cannot move non-Copy storage out of a source collection
that `map` only READs. Moss rejects the operation instead of synthesizing a copy or
leaving the failure to generated Rust. Capture effects are interprocedural: mutation
hidden behind one or more ordinary helper calls is treated exactly like direct captured
mutation.

A pipeline is internal compiler structure until it becomes a materialized Moss
collection or terminal result. A transformation pipeline cannot directly cross a
`message` or `reply` copy boundary; materialize it in a local binding first.
A terminal is already a concrete scalar and may cross an explicit domain boundary
normally. The boundary never transports a lazy pipeline object.

## Observable effects and fusion

Ownership effects say how storage is accessed. A separate observable-effect summary says
whether changing the schedule is visible. It records:

- immutable/local capture reads;
- local mutation;
- domain-state observation and mutation;
- synchronous `message`;
- external or I/O effects such as `echo`;
- possible failure and unresolved effects;
- potential divergence, tracked separately from both failure and observable effects.

Arithmetic, comparisons, local temporaries, immutable capture reads, and transitively
equivalent calls can be fusion-safe. Mutation, domain effects, communication, I/O,
unresolved calls, and potentially different failure ordering are barriers. In
particular, division or indexing remains a conservative `may_fail` barrier when the
compiler lacks a proof that it cannot fail.

Phase 4.5 also distinguishes invocation-preserving fusion from work-skipping
optimization. A function, method, or handler containing a `while` is conservatively
`may_diverge`, as is a callable that transitively calls one. The fact propagates through
the existing acyclic local call graph. The current analysis does not try to prove that a
loop terminates. Placeholder expressions with no call to such a callable are
non-divergent.

Potential divergence alone does not stop ordinary ordered map/filter fusion: that
lowering preserves the callback computations needed by the eager program. A
transformation that can execute fewer callbacks has a stronger proof obligation. Dead
map elimination and `any`/`all` short circuiting require every skipped callback to be
free of observable effects, observable failure, and potential divergence. The same rule
governs a completed `any`/`all` branch inside a shared-source traversal.

This preserves eager ordering for effectful callbacks. For a safe chain, `-O` can turn:

```moss
values |> map(f) |> filter(p) |> map(g) |> sum
```

into one explicit loop with a wrapping accumulator and no intermediate collection. `-O0`
executes explicit stage-at-a-time loops and materializes each logical transformation.
Both paths implement the same source contract.

## Functional IR and provenance

`src/functional_ir.hpp` defines focused `Source`, `Map`, `Filter`, `Reduce`, `Sum`,
`Count`, `Any`, and `All` nodes. Each node retains:

- a dense numeric ID used only as a compilation-local plan handle;
- a separate semantic identity derived from the owning function/method/handler context
  and real Moss source occurrence;
- Moss line and stage span;
- concrete input and output types;
- callable identity, expression, and captures;
- ownership and observable-effect summaries;
- logical materialization and whether it was eliminated;
- source-node provenance.

The checked AST carries the exact compilation-local `functional_pipeline_id` for every
emitted expression and static specialization. Rust generation consumes that ID directly;
it never rediscovers a plan by comparing expression text, inferred types, or callable
names. This matters when identical source text has different effects in two contexts.

The optimizer also records element independence, determinism, and reduction
compatibility. A fused loop carries all source semantic identities, preserving enough
origin data for later source mapping and semantic dependency work without implementing
Phase 5 or 6.

Use deterministic development output to inspect the representation:

```sh
./moss -O --dump-functional-ir --check examples/functional_dataflow.moss
./moss -O --explain-fusion --check tests/phase4_effect_smoke.moss
```

The Rust emitter executes the completed plan. Fused output is deliberately simple Rust
control flow, not an opaque iterator abstraction. Generated comments identify eager and
fused pipelines and list their retained provenance.

## Phase 4.5 scope-level optimization

Phase 4.5 augments the checked Phase 4 nodes; it does not resolve callables, effects,
ownership, or failure behavior again. The planner derives use counts, lexical liveness,
escape facts, materialization requirements, consumer edges, and shared-traversal
eligibility from the authoritative semantic plans.

Materialization and traversal fusion are separate decisions. Every logical `map` or
`filter` result receives a plan such as `Virtual` or `Materialize`, plus facts recording
escape, multiple consumers, and eager barriers. A fused non-terminal pipeline still
materializes its final returned collection, even though collections solely connecting
internal stages are virtual. `--dump-functional-ir` prints both the decision and reason.

For an exact-size vector source, optimized `count` reads `len` directly. A chain of
pure, non-failing maps followed only by `count` also becomes the original source length;
the unused mapped values and callback executions disappear. This is forbidden when a
callback has an observable effect, may fail, or may diverge.

The eager reference lowering evaluates every `any`/`all` predicate in source order.
Optimized lowering may stop a terminal's computation at its first decisive element only
when all skipped work is pure, non-failing, and non-divergent. Inside a shared traversal, another
terminal may still require the source loop to continue; the completed `any`/`all`
consumer is then disabled for later elements.

The first cross-binding rule is intentionally narrow:

```moss
let normalized = values |> map(normalize)
total = normalized |> filter(valid) |> map(score) |> sum
```

When the binding is immutable, has exactly one use, is in the same lexical block, and
the consumer is immediately adjacent with no effect, ownership, failure, or control-flow
barrier, its physical collection is virtual. The source binding remains part of Moss's
meaning and diagnostics. A later independent use, second consumer, `var`, reassignment,
or intervening observable statement forces materialization.

Compatible adjacent terminal pipelines over the exact same stable local source form a
small explicit DAG:

```text
          Source
         /  |  \
      Sum Count Filter -> Count
```

The generated Rust initializes each terminal accumulator and updates them in one simple
loop. The initial implementation requires pure, non-failing paths, new local result
bindings, no result dependency, no intervening statement, and no source mutation. It
does not reorder stages, push predicates, duplicate work, or apply a general
profitability model. Each DAG retains the semantic identities and provenance of all
consumer pipelines.

## Phase 4.6 semantic-space optimization

Moss semantic-space optimization is a Moss-to-Moss rewrite phase. It preserves a
functional computation in semantic form long enough to replace it with an equivalent
Moss computation that performs less semantic work before normal compilation and
lowering.

The checked Phase 4 nodes remain authoritative for types, callable identities,
ownership, effects, failure, divergence, and source provenance. Phase 4.6 adds a small
post-analysis `semantic_steps` sequence. A step points back to one or more original
nodes, so adjacent maps or filters can be represented as a composed Moss operation
without discarding their individual source identities. Rust generation consumes this
exact sequence from the checked pipeline ID. It never reconstructs a rewrite by
matching source text.

The bounded pass order is:

1. normalize one semantic step per checked functional node;
2. remove a trivial identity map when that proves predicate pushdown;
3. compose adjacent safe maps and adjacent safe filters;
4. apply terminal dead-work, cardinality, and `any`/`all` plans;
5. link already-proven single-use lexical producers and consumers;
6. revalidate terminal work skipping across those links;
7. form compatible shared-source DAGs;
8. finalize materialization and provenance plans.

The implementation performs one structurally bounded sequence rather than an unbounded
fixed point or a generic rewrite engine. `--dump-functional-ir` shows both the optimized
semantic plan (`ComposedMap`, `ComposedFilter`, and remaining stages) and deterministic
`semantic-opt:` records. Original nodes remain visible and identify eliminated semantic
work.

Map composition retains the original callable order. Filter composition lowers as
left-to-right short-circuit conjunction, so the second predicate still runs only for
values accepted by the first. Potential divergence alone does not prohibit these
invocation-preserving rewrites when all otherwise observable effects and failures are
absent. In contrast, dead-map removal and terminal short circuiting execute fewer
callbacks and therefore require `safe_to_skip`: no observable effect, failure, or
potential divergence.

Predicate pushdown is deliberately proof-bounded. The current IR can prove that
`map(_)` over a trivial element type is identity, so a following filter may consume the
earlier value and the redundant map may disappear. Moss does not assume a named
identity function is identity and does not move a predicate across an arbitrary or
type-changing map. General algebraic predicate rewriting remains deferred.

Terminal-aware plans retain only computation needed for the result. In particular,
`filter |> map(pure) |> count` removes the trailing map but retains the cardinality-
changing filter, while a map-only count becomes source length. A count over an inert
scalar vector literal can become its constant cardinality without constructing the
literal. Runtime-empty collections continue to observe the defined identities for
`sum`, `count`, `any`, `all`, and initialized `reduce`; Phase 4.6 does not add an
untyped-empty-literal exception to collection inference.

This phase is distinct from lazy evaluation. Logical Moss source semantics remain eager
and `-O0` remains the stage-at-a-time reference. The optimizer changes which equivalent
computation is required only after proving that omitted materialization or callback work
is unobservable.

The architectural boundary is simple: if an optimization can be described as “this
equivalent Moss computation asks for less work,” it belongs in semantic-space
optimization. Decisions about SIMD, threads, GPUs, loop tiling, caches, instruction
selection, LLVM, and physical class locking belong downstream.
Phase 4.6 introduces neither MLIR nor a general optimizer/plugin framework; the focused
semantic IR remains sufficient for this bounded pass set.

## Imperative static iteration

Phase 4.7 adds imperative `for` loops without changing eager pipeline semantics:

```moss
for value in values:
  process(value)
```

The compiler resolves iteration statically. `Vector` and `range(start, end)` use
compiler-native Rust traversal; user-defined traversal resolves directly to a concrete
`next() -> Option[element]` method. There is no runtime iterator dispatch or vtable.
Pipelines remain the existing eager functional/dataflow form and are not silently
converted into `for`-loop IR.

## Executable examples

The examples directory separates the main ideas into small programs:

- `functional_basics.moss` covers named functions, placeholders, immutable captures,
  and reuse of the READ-only source.
- `functional_reductions.moss` covers every terminal, empty-input identities, explicit
  reduction initializers, and wrapping integer accumulation.
- `functional_static_callables.moss` covers bound methods, method placeholders, and
  compile-time specialization of a higher-order helper.
- `functional_effect_order.moss` makes eager callback and initializer ordering visible
  with `echo`, demonstrating why observable effects stop fusion.
- `functional_objects.moss` applies concrete read-only methods and safe trivial-field
  projections to user-defined values without dynamic dispatch or hidden object copies.
- `functional_domains.moss` demonstrates a synchronous domain message inside a callback as a fusion barrier
  and then sends the concrete terminal result across a normal message boundary.
- `functional_terminal_optimization.moss` demonstrates exact counts, dead pure maps,
  and legal short-circuit terminals.
- `functional_scope_fusion.moss` demonstrates a virtual single-use immutable binding.
- `functional_shared_traversal.moss` demonstrates a shared-source terminal DAG.
- `functional_materialization.moss` demonstrates a multiple-consumer materialization
  followed by a legal shared terminal traversal.
- `functional_semantic_optimization.moss` demonstrates composed maps and filters,
  identity-only predicate pushdown, a virtual named intermediate, terminal dead-map
  removal, literal cardinality, and safe short circuiting.

Each program is compiled and executed through both `-O0` and `-O` by the regression
suite. Their observable output must agree.

## Deliberately deferred

Phase 4/4.5/4.6 do not add automatic parallelism, SIMD, GPU code generation, a dynamic
callable runtime, general lambda syntax, source effect annotations, initializer-free
reduction, generic stage reordering, algebraic predicate pushdown, or new failure
semantics. Phase 4.6 supports only identity-proven pushdown. The IR preserves the
topology, concrete element types, captures, reductions, independence, materialization,
semantic rewrite provenance, and consumer-graph facts that later optimizers will need.
