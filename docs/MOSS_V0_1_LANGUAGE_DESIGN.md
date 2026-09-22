# Moss

## Julia-Like Ergonomics with Static Safety and Compiler-Derived Concurrency

*Language Design, Formal Safety Model, and v0.1 Implementation Experience*

**Kutty Banerjee**  
September 2026

> **Moss v0.1 thesis.** Write code with the economy of a high-level language; close types, call targets, ownership effects, domain topology, and synchronization statically; lower the result to direct safe Rust. The compiler, not the programmer, chooses Moss-managed locks and their order.

---

## Abstract

Moss is a compiled language designed to feel closer to Julia than to a traditional systems language while still producing a statically closed native program. Programmers can omit many type annotations, use structural traits without writing implements declarations, and rely on specialization instead of runtime dynamic dispatch. Before native code generation, however, Moss closes the concrete types, call targets, ownership effects, domain topology, and synchronization requirements of the reachable program. Moss compiles to safe Rust, deliberately reusing Rust's mature memory-safety discipline and native backend while restricting selected concurrency patterns further so that lock choice, lock ownership, and Moss-managed lock order become compiler facts rather than programmer decisions. The central concurrency abstraction is the domain: a statically composed owner of mutable state entered only through synchronous message. Programmers write neither mutexes nor atomics. For each concrete handler, the compiler infers read/write/consume effects, derives a conservative static `ClassSet`, partitions protected state into synchronization classes, and emits direct typed RwLock<ClassState> fields. The compiler selects static acquisition placement; touched classes remain held through completion, while Phase 15.3 can defer and cancel branch-only untouched classes in a bounded conditional shape. This supports a structural deadlock-freedom argument for Moss-managed locks and whole-execution conflict serializability for failure-free same-domain handler executions that dynamically conflict on domain-owned semantic locations. The paper separates four concerns. Part I presents Moss as a language: “infer all the way, then close statically,” structural traits, ownership effects, domains, synchronous messaging, the programmer-visible consistency model, and features that make the language unusually tractable for AI-assisted and agentic coding. Part II states the formal model, including observable non-domain effects, conservative path-insensitive effect inference, synchronization classes, static-footprint strict two-phase locking, global lock ranking, and proof assumptions. Part III describes the v0.1 implementation and its design corrections, including the replacement of mailbox/await semantics, closure of the domain-handle universe, zero-copy protected reads, zero-copy borrowed lowering for eligible synchronous inter-domain message payloads, and the final static typed lowering that reduced lock-wrapper overhead to the same cost class as equivalent handwritten Rust. Part IV places Moss against prior lock inference, effect systems, actors, DPJ, Pony, Julia, and Rust, and states the limits of the current claims.

## Contents

- [Part I - The Language](#part-i---the-language)
  - [1. The simple pitch: write high-level code, compile a closed systems program](#1-the-simple-pitch-write-high-level-code-compile-a-closed-systems-program)
  - [2. A small surface with structural specialization](#2-a-small-surface-with-structural-specialization)
  - [3. Ownership effects are inferred, not spelled with parameter modifiers](#3-ownership-effects-are-inferred-not-spelled-with-parameter-modifiers)
  - [4. Functional/dataflow code without general closure objects](#4-functionaldataflow-code-without-general-closure-objects)
  - [5. Domains: state ownership as an architectural construct](#5-domains-state-ownership-as-an-architectural-construct)
  - [6. Static composition and immutable routing](#6-static-composition-and-immutable-routing)
  - [7. Synchronous message and terminating reply](#7-synchronous-message-and-terminating-reply)
  - [8. Programmer-visible consistency and ordering](#8-programmer-visible-consistency-and-ordering)
  - [9. By-value domain boundaries](#9-by-value-domain-boundaries)
  - [10. Observable effects beyond domain state](#10-observable-effects-beyond-domain-state)
  - [11. Safety by restriction: Rust strengths plus additional static concurrency constraints](#11-safety-by-restriction-rust-strengths-plus-additional-static-concurrency-constraints)
  - [12. Diagnostics are part of the language product](#12-diagnostics-are-part-of-the-language-product)
  - [13. Modules and semantic interfaces](#13-modules-and-semantic-interfaces)
  - [14. One frontend, two execution engines](#14-one-frontend-two-execution-engines)
  - [15. Features for AI-assisted and agentic coding](#15-features-for-ai-assisted-and-agentic-coding)
  - [16. Intentional v0.1 boundaries](#16-intentional-v01-boundaries)
  - [17. Future domain scopes: an intentionally open breadcrumb](#17-future-domain-scopes-an-intentionally-open-breadcrumb)
- [Part II - Formal Model and Safety Arguments](#part-ii---formal-model-and-safety-arguments)
- [Part III - Implementation Design, Corrections, and Measurements](#part-iii---implementation-design-corrections-and-measurements)
- [Part IV - Related Work and Positioning](#part-iv---related-work-and-positioning)
- [Appendix A - Compact v0.1 semantic summary](#appendix-a---compact-v01-semantic-summary)
- [Appendix B - Proof assumptions checklist](#appendix-b---proof-assumptions-checklist)
- [Appendix C - Moss v0.1 milestone map](#appendix-c---moss-v01-milestone-map)
- [Appendix D - References](#appendix-d---references)

---

## Part I - The Language

### 1 The simple pitch: write high-level code, compile a closed systems program

The simplest way to describe Moss is: write it like a high-level language, compile it like a systems language. A Moss function can look Julia-like—few annotations, structural constraints instead of nominal conformance declarations, and specialization based on concrete use—but the compiler refuses to leave dynamic uncertainty unresolved at native code generation. Moss attempts to infer all the way down: types, call targets, ownership effects, concrete domain identities, routing, and synchronization are derived when the program provides enough information; unresolved behavior is rejected before the final native program is emitted.

Moss is not Julia. Julia intentionally supports dynamic dispatch and runtime method selection; Moss v0.1 deliberately does not. The resemblance is ergonomic rather than semantic: source should describe the computation rather than repeat facts the compiler can infer. The compiled result is closer in spirit to a fully monomorphized systems program.

Moss is also not a replacement for Rust's safety model. It compiles to safe Rust and leverages Rust's ownership-oriented type system, standard library, native code generation, and tooling strengths. Moss narrows the source language further in selected areas. In particular, programmers do not select Moss-managed locks, do not write lock order, cannot dynamically rewire domain references, and cannot pass domain handles as ordinary values. The resulting claim is deliberately scoped: within the Moss domain model, additional concurrency mistakes can be ruled out by construction because the relevant choices are absent from the language surface.

> **Key idea.** Moss's design principle is infer all the way, then close statically. Source code may remain locally under-specified; the native program may not.

### 2 A small surface with structural specialization

Moss supports primitives, value-oriented objects, aggregates, collections, ordinary functions, traits, modules, functional/dataflow operators, and domains. Not every function boundary must spell out a concrete type. Untyped parameters are statically specialized at concrete uses.

Traits are structural compile-time predicates rather than nominal memberships. A type does not say that it implements a trait; conformance is checked when a concrete type reaches a use that requires the trait.

```moss
trait Drawable:
  fn area() -> Int
  fn draw(canvas: Canvas) -> Int

type Circle:
  radius: Int

  fn area():
    return radius * radius

  fn draw(canvas: Canvas):
    return radius * radius * canvas.scale

fn render(x: Drawable, canvas: Canvas):
  return x.draw(canvas)
```

A `Circle` satisfies `Drawable` because the required operations match. No `implements Drawable` declaration is needed. A type may therefore satisfy many traits without being coupled to their declarations.

Moss v0.1 does not have runtime trait objects or general dynamic dispatch. Structural conformance is resolved during specialization. This is important to the larger thesis: the language can feel duck-typed locally while still closing statically before native code generation.

### 3 Ownership effects are inferred, not spelled with parameter modifiers

Moss uses three semantic access effects:

| Effect | Meaning |
|---|---|
| `read` | May observe the caller's storage/value. |
| `write` | May mutate caller-visible storage. |
| `consume` | May take ownership of the current value under Moss ownership rules. |

Effects are inferred from the specialized body and propagated through ordinary calls. There is no planned `mut`, `inout`, or `write` parameter modifier merely to communicate these facts to the compiler.

At a call site, the compiler checks capabilities. `read` requires readable storage/value; `write` requires legal mutable caller storage; `consume` requires ownership/move capability. Overlapping actual arguments are accepted for `read`+`read` and rejected whenever overlap involves `write` or `consume`. A literal or temporary cannot satisfy a caller-visible `write` formal.

This design keeps source lightweight while retaining static closure. It also makes synchronization derivation possible later: transitive writes through helpers are visible to the handler effect summary even when the handler body does not contain the mutation syntactically.

### 4 Functional/dataflow code without general closure objects

Moss supports concise functional/dataflow forms such as map, filter, reduce, sum, count, any, and all, including restricted placeholder/capture forms. Static callable specialization occurs before effect derivation, and captures of domain state contribute to handler effects.

Moss v0.1 does not provide general first-class closure values or runtime higher-order dispatch. A bound callable that participates in a functional pipeline is statically resolved. This distinction matters because the implementation can reason about a finite set of concrete callable bodies while still offering concise dataflow syntax.

Ordinary recursion is also disallowed in v0.1, but not for the reason that synchronization-effect closure would be impossible. With a finite effect lattice, recursive call-graph SCCs can be solved by a least fixed point. The v0.1 restriction is instead an implementation/language-surface choice: recursion raises separate questions about specialization termination, inference ergonomics, diagnostics, and unbounded stack depth. The synchronization proof does not require ordinary recursion to remain forbidden. Domain-level recursive call structure is different: the concrete domain routing graph is required to be acyclic.

This distinction is important for dogfooding. Tree walks, parsers, compiler passes, and other naturally recursive programs may provide early evidence that ordinary recursion deserves a later phase.

### 5 Domains: state ownership as an architectural construct

A domain owns mutable state and exposes handlers as the only externally callable operations on that state. A domain is not defined by a mailbox or worker thread. In the final v0.1 model, a domain is a static ownership and synchronization boundary.

Domain handlers use the same `fn` declaration syntax as ordinary functions; being declared inside a `domain` makes them message-entry handlers. Moss does not use a separate `on` handler keyword.

The paper uses one running example throughout:

```moss
domain Account:
  balance: Int
  risk_limit: Int
  display_name: String
  stats: Stats
  config_value: Int

  fn rename(name):
    display_name = name

  fn set_risk_limit(limit):
    risk_limit = limit

  fn record_fill(fill):
    balance = balance + fill.amount
    stats = update_stats(stats, fill)
    validate_risk(balance, risk_limit)

  fn read_stats():
    reply stats

  fn read_config():
    reply config_value
```

The programmer did not write a lock. The compiler will eventually discover that `config_value` is immutable after publication, while the other fields participate in different synchronization classes.

### 6 Static composition and immutable routing

Domains are wired in an initial composition prefix. A domain declares outbound routing dependencies with `domainroutes(...)`. Domain handles are routing capabilities, not ordinary values.

```moss
domain App:
  domainroutes(account: Account, logger: Logger)

  fn run():
    receipt = message account.read_stats()
    message logger.record(receipt)

fn main(config):
  account = Account(
    balance = config.start_balance,
    risk_limit = config.limit,
    display_name = config.name,
    stats = initial_stats(),
    config_value = config.mode)

  logger = Logger(...)
  app = App(account = account, logger = logger)

  message app.run()
```

The number of concrete domain instances, their identities, and every route binding are statically enumerable. Initialization values may depend on runtime configuration through pure expressions; topology may not depend on runtime control flow.

Domain handles cannot be passed as ordinary function parameters, handler payloads, reply values, collection elements, mutable state, or aliases. Every legal cross-domain message target is therefore attributable to the closed concrete domain graph.

#### Terminology note

Chapel also uses the term domain, but for a different language construct: Chapel domains are first-class index sets used to define array index spaces. Moss domains are state-owning architectural/synchronization units. The shared word should not be read as a semantic relationship.

### 7 Synchronous message and terminating reply

`message` is the only operation that enters a domain handler. It is synchronous, blocking, and expression-valued:

```moss
stats = message account.read_stats()
message logger.record(stats)
```

There is no domain-level `await` and no fire-and-forget send in v0.1. A `message` in statement position simply discards the result; it remains synchronous.

`reply` is terminating. Its expression is evaluated before handler completion; an independent semantic result is established; the handler terminates; then the caller resumes. The implementation must retain any synchronization required to preserve the whole-execution serializability guarantee of Section 8, and any guard that physically backs a borrowed lowering, through reply materialization. In the v0.1 full-hold lowering, all guards acquired for the handler remain held until the independent reply result has been established and the handler completes. No statement after a taken reply executes.

Incoming message payloads are immutable snapshots. Handlers may read them, forward them through another message (creating a new value boundary), or reply with them (again creating a new value boundary), but may not mutate or consume the original incoming snapshot.

### 8 Programmer-visible consistency and ordering

Moss intentionally does not promise sequential consistency across the entire domain graph.

At the language level, an executed state access targets a **domain-owned semantic location**. Two failure-free handler executions on the same domain **dynamically conflict** when they both actually access the same semantic location and at least one of those executed accesses writes or consumes it. Conflict is therefore defined by the accesses that occur in the execution, not by the compiler's conservative static may-footprint, synchronization classes, or lock layout.

Failure-free completed handler executions on one domain admit a **single conflict-serialization order** for their domain-owned state: the observed protected-state behavior is equivalent to a serial execution in that order. For every pair that dynamically conflicts, the observed behavior of their **whole executions** is equivalent to one in which all actions of one precede all actions of the other, with that whole-execution order consistent with the same domain conflict-serialization order. This is an observational serializability guarantee, not a requirement that every internal state access of the two executions occur in literal wall-clock non-overlap. For this guarantee, a handler's whole execution includes its own state accesses, nested synchronous messages and their descendant executions, observable non-domain effects such as `echo`, and reply materialization before handler completion. Nonconflicting executions need not be ordered as wholes and may overlap or interleave.

The v0.1 backend is deliberately more conservative than this semantic contract. It maps semantic locations to a finite set of static analysis leaves, derives a statically inferred `ClassSet`, acquires that complete set before the body, and holds those guards through completion. Two executions can therefore serialize even when their realized semantic accesses would not dynamically conflict. That extra serialization is an implementation consequence of conservative synchronization, not source-level Moss semantics.

Synchronous calls also impose ordinary source order along a call chain: if handler A calls B and waits for its reply, B completes before A continues. Across independent domains, however, there is no single global order of all observable actions. Two independently executing handlers may produce effects that downstream observers see in different orders unless a same-domain conflict or an explicit synchronous dependency orders the relevant executions. Moss therefore does not promise graph-wide sequential consistency or a multi-domain transaction. Whole-execution serializability between conflicting same-domain executions does not prevent unrelated third-party handlers in descendant domains from running where their own synchronization permits.

This distinction between semantic conflict and conservative implementation footprint is part of the language model, not merely a non-claim hidden in the proof section.
### 9 By-value domain boundaries

Message arguments and reply results are **semantic by-value boundaries**. The programmer can rely on value independence across a domain boundary: a callee cannot retain a mutable alias into the caller's state, mutate the caller's payload through an alias, or consume the caller's incoming snapshot.

By-value semantics do **not** require a physical copy on every synchronous message.

Because `message` is synchronous and incoming payloads are immutable and non-consuming, the shared-memory backend may lower an eligible inter-domain payload to an immutable Rust borrow such as `&T`, or to a typed borrowed view for decomposed state. The borrow exists only for the dynamic extent of the synchronous message call and cannot escape into Moss-visible state.

Conceptually:

```text
Moss semantics:
    caller value --by-value message boundary--> immutable callee snapshot

Possible Rust lowering:
    caller storage --temporary &T / borrowed view--> callee
```

The two are observationally equivalent under the Moss rules: the receiver cannot mutate or consume the payload, cannot retain the borrow after the call, and the sender resumes only after the callee returns.

This optimization also applies transitively to forwarding when the same payload is passed through a chain of synchronous messages and the compiler can prove the borrow remains valid. If those conditions are not available, the backend conservatively materializes an owned value instead.

Small `Copy` values may simply be passed by value. Foreign/native boundaries and other cases where the compiler cannot prove safe borrow lifetime or representation also materialize an owned value.

Replies are different: the caller uses the result after the callee has returned, so reply results remain owned semantic values rather than borrowed results.

A Rust borrow used to implement a message is therefore a **physical lowering detail**, not a source-level Moss reference. If the borrowed payload is backed by protected domain state, the guard protecting that storage must remain held for the complete dynamic extent of the synchronous message call; a guard may not be dropped while a borrowed view derived from it remains live. Moss exposes no cross-domain mutable alias, source-level lifetime, or borrowed-message type. Callables themselves do not cross message or reply. This keeps the cross-domain boundary first-order while allowing the native backend to avoid unnecessary copies.

### 10 Observable effects beyond domain state

Synchronization effects and all observable effects are not the same thing. Moss derives `read`/`write`/`consume` over domain-owned state because those effects drive synchronization. We separately write $O(h)$ for the observable non-domain-state effects of handler h: output such as `echo`, and in future versions foreign I/O or system effects crossing an interoperability boundary.

The v0.1 lock planner does not partition on $O(h)$; observable effects are not themselves synchronization-footprint elements. They are nevertheless part of the **whole execution** defined in Section 8. Therefore, when two same-domain handler executions dynamically conflict on a domain-owned semantic location, the observed behavior of their $O(h)$ actions, nested messages, state accesses, and reply materialization must be equivalent to a whole-execution order consistent with the domain's single conflict-serialization order. Observable actions such as `echo` must therefore appear in an order compatible with that serialization position even though commuting internal state accesses may physically overlap.

This does not create a global order on observable effects. Nonconflicting executions may overlap and their $O(h)$ actions may interleave; independently executing handlers in different domains may expose $O(h)$ effects in different orders unless a same-domain conflict or an explicit synchronous dependency orders the relevant executions. The v0.1 full-hold implementation realizes the stronger same-domain guarantee by keeping the handler's acquired guards across its complete body, including nested messages and `echo`.
### 11 Safety by restriction: Rust strengths plus additional static concurrency constraints

Moss compiles to safe Rust and relies on Rust's memory-safety and data-race guarantees in safe code. The contribution is not that Moss somehow makes Rust's existing guarantees conditional or stronger in every dimension. Rather, Moss removes additional concurrency choices from normal source code.

| Concern | Safe Rust | Moss v0.1 |
|---|---|---|
| Memory/ownership | Ownership and borrowing prevent broad classes of memory errors | Compiles to safe Rust plus inferred Moss ownership/capability checks |
| Data races | Prevented in safe Rust by the type system and safe synchronization/interior-mutability abstractions | Also prevented; Moss additionally derives the synchronization objects and access modes for domain state |
| Forgotten/wrong lock | Possible logic error in otherwise safe Rust | No user lock selection exists for Moss-managed domain state |
| Lock-order deadlock | Possible | Ruled out for Moss-managed domain locks under the static graph/rank assumptions |
| Dynamic route/lock graph | Fully programmable | Concrete domain routing graph is statically closed and acyclic |
| Dispatch | Static and dynamic mechanisms available | No runtime dynamic dispatch in v0.1 |
| Recursion | Allowed | Ordinary recursion is a v0.1 restriction for specialization/implementation reasons, not the lock proof |
| Concurrency topology | General-purpose | Deliberately restricted static domain topology |

*Table 1: Moss trades generality for compiler ownership of additional concurrency decisions.*

The useful edge over Rust is therefore concentrated in the middle of the table: a programmer cannot forget the Moss-managed lock, choose the wrong one, or establish an inconsistent Moss lock order, because those operations are not part of the Moss source language.

### 12 Diagnostics are part of the language product

A language that removes programmer-written locks must explain the restrictions and rewrites that make the proof possible. Representative diagnostics should be concrete and teach the model.

```text
error: same-domain handler entry is not permitted
  message account.refresh()
          ^^^^^^^
'account' is the currently executing domain instance.

rewrite: extract shared same-domain logic into an ordinary helper
```

```text
error: domain handle cannot cross a message boundary
  message worker.run(database)
                     ^^^^^^^^
'database' is a domain routing capability, not a value payload.

rewrite: declare an immutable domainroutes dependency on Worker
```

```text
error: concrete domain routing cycle
  app   --cache--> cache
  cache --store--> store
  store --owner--> app

Moss requires the concrete domain routing graph to be acyclic.
```

These are not merely nicer error messages. They are the mechanism by which a restricted safety model remains usable.

### 13 Modules and semantic interfaces

Moss v0.1 supports explicit modules, project builds, typed exports, semantic interfaces, and source-free production providers. The module system is usable but intentionally not declared final; dogfooding is expected to reveal where import ergonomics, package discovery, and cross-module specialization need improvement.

The key separation is semantic versus physical ABI. A provider can export checked type/effect information sufficient for downstream specialization. Physical synchronization classes, lock ranks, Rust lifetimes, and generated lock layout are not module ABI. The final application derives them from its concrete graph and emits physical synchronization itself.

### 14 One frontend, two execution engines

Moss has one checked semantic frontend and two execution engines:

```text
Moss source
    |
    v
Checker + specialization + effects + concrete domain graph
    |                                      |
    v                                      v
Production                            Fast Debug
SynchronizationPlan                  deterministic semantic interpreter
    |
    v
typed Rust + 2PL
```

Production assumes concurrent handler entry may occur and therefore uses physical synchronization. Fast Debug executes the same checked domain semantics directly and deterministically without simulating locks, threads, or alternative schedules. Its structured trace exposes source identity, concrete instance identity, handler entry/exit, state reads/writes, messages, replies, and branches.

### 15 Features for AI-assisted and agentic coding

Moss is not designed around the premise that an AI agent should be trusted to generate more low-level machinery. The opposite is more useful: the language removes choices that are expensive for both humans and agents to get right, then exposes the compiler's semantic knowledge in forms an agent can inspect. The result is a smaller legal program space, a deterministic debug loop, and fewer multi-step stateful interactions with external debuggers.

This matters because agentic coding has a different failure profile from ordinary interactive programming. An agent can generate plausible code quickly, but it is vulnerable to hidden dynamic behavior, tool-state drift, ambiguous diagnostics, and long sequences of debugger commands whose intermediate state must be remembered correctly. Moss's restrictions and tooling reduce those sources of uncertainty without changing the semantic contract for human programmers.

| Feature | Why it helps an AI coding agent | v0.1 status |
|---|---|---|
| Static closure | Concrete call targets, domain instances, routes, effects, and synchronization are resolved before native code generation. The agent reasons about a finite checked program rather than an open runtime dispatch space. | Implemented |
| Restricted concurrency surface | The agent does not invent locks, lock order, actor sharding, or dynamic routing schemes for ordinary Moss domain state. Those decisions are compiler-derived. | Implemented |
| Structural traits and inference | Agents can write concise code without manufacturing nominal conformance declarations or redundant generic type variables merely to satisfy the compiler. Errors are discovered when concrete uses fail to specialize. | Implemented |
| Stable semantic identities | Source entities and concrete specializations have stable compiler identities/provenance that can be surfaced to tools instead of reconstructed from textual guesses. | Implemented |
| Semantic introspection | The checked compiler model can answer machine-oriented questions about specializations, effects, concrete domain topology, routes, and synchronization plans. | Implemented |
| Deterministic Fast Debug | An agent can execute checked Moss semantically without managing a native debugger session, thread schedule, lock timing, or process-control state. | Implemented |
| Structured execution trace | Handler entry/exit, branches, messages, replies, and state accesses can be represented as structured events suitable for search, slicing, comparison, and model context. | Implemented baseline |
| Machine-teaching diagnostics | Restrictions can be reported in terms of the offending construct, the language rule, and a legal Moss rewrite, turning compiler failures into a teaching channel for agents. | Future Phase 22 expansion |
| Agent benchmark suite | Representative tasks can measure first-attempt compile rate, iterations-to-green, error classes, partition quality, and context efficiency instead of relying on anecdotes. | Future Phase 22 |

*Table 2: Moss properties that reduce ambiguity and tool-state burden for AI-assisted programming.*

#### A deterministic debug loop instead of an interactive debugger protocol

Traditional native debugging is often an interaction protocol: set a breakpoint, run, inspect, step, print, continue, and repeat. For an agent, each step is another tool call and another piece of ephemeral process state that must remain synchronized with its reasoning. Moss's Fast Debug engine converts many small-scope debugging problems into a data-analysis problem: run the checked program deterministically, emit a structured trace, and reason over the resulting execution record.

The intended agent loop is therefore closer to:

```text
edit Moss source -> check/specialize -> run Fast Debug -> inspect diagnostics + trace -> edit
```

rather than "drive LLDB correctly for twenty turns." This does not make trace volume free. Long-running loops can produce more information than a model should consume directly, so trace slicing, filtering, and semantic queries remain important tooling work. The advantage is that the underlying execution record is deterministic and machine-readable rather than an implicit debugger session.

#### The compiler as a semantic oracle

Because Moss closes the reachable program before code generation, the compiler already knows facts that an agent would otherwise have to infer approximately from source text: which concrete callable a use selects, what state a handler may read/write/consume, which concrete domain instance a route names, which handlers conflict, and which synchronization classes a handler acquires. Exposing those facts through semantic queries lets an agent ask the compiler instead of reverse-engineering the compiler.

This is a deliberate design direction, but it should not be confused with changing Moss semantics for AI. The language remains human-readable and its safety arguments do not depend on an AI system. Agent tooling consumes the same checked semantic objects used for native compilation and Fast Debug.

#### Restrictions can improve generation quality

Several v0.1 restrictions are useful to agents for the same reason they are useful to static analysis. Domain handles cannot flow through arbitrary values; routing is immutable; the concrete domain graph is closed; general dynamic dispatch is absent; Moss-managed synchronization is not programmable; and message boundaries are explicit. These rules remove large families of plausible-but-invalid implementation strategies from the search space.

The claim is deliberately modest: Moss does not make an agent correct, and v0.1 is not presented as an "AI programming language." Rather, Moss gives an agent a more constrained target language and richer compiler feedback. The expectation is that this can improve first-attempt validity, reduce repair turns, and make debugging less dependent on fragile interactive state. Phase 22 is reserved for measuring and improving those properties after the language has first been dogfooded as a language.

### 16 Intentional v0.1 boundaries

The following are deliberate v0.1 limits, not accidental omissions hidden by the paper:

- no runtime dynamic dispatch or general first-class closures;
- no ordinary recursion yet (an implementation/language-surface restriction, not a synchronization-proof requirement);
- no dynamic domain creation, first-class domain handles, or topology-affecting runtime control flow;
- no source-level thread/task ingress construct yet;
- no asynchronous domain messaging;
- no programmer-written Moss lock/atomic syntax;
- no finalized Rust interoperability or supervision/error-recovery model;
- no dynamic key-based lock partitioning inside runtime-indexed aggregates.

Concurrent ingress is a deliberate exclusion. v0.1 specifies what is true once control enters a domain handler; it does not yet specify a source-level mechanism that creates independent concurrent roots. The production backend nevertheless assumes concurrent handler entry is possible and may not erase synchronization merely because main appears sequential.

### 17 Future domain scopes: an intentionally open breadcrumb

Current v0.1 composition effectively creates domains for the lifetime of the enclosing program scope. The synchronization plan is intentionally graph-relative rather than process-global so that future work can explore lexical domain scopes and repeated graph activations.

An attractive future possibility is an inner scope that statically imports an outer routing anchor during composition, while preventing any inner handle from escaping upward. However, the rank composition and repeated-activation semantics are intentionally unresolved. Moss v0.1 does not choose between independent nested subgraphs and nested graphs that may route to longer-lived outer domains.

## Part II - Formal Model and Safety Arguments

### Concrete built-in collection types

Moss v0.1 has no user-defined/source generic type variables or templates. This
does not prohibit concrete uses of built-in collection types: `Vector[Int]`,
`Map[String, Int]`, and `Queue[Int]` are concrete type positions, not generic
programming. Locals are normally inferred; `Vector[T]()` is the supported typed
empty-vector constructor, while local declaration annotations remain unsupported.

### 18 Static objects of the model

For a checked graph context $G$, let $D$ range over concrete domain instances. Each instance has a concrete specialization and a finite set of handlers $H_D$.

The **semantic locations** of $D$ are the domain-owned storage identities addressed by executed Moss operations. They define source-level conflict. A scalar field ordinarily denotes one semantic location. An element- or key-addressed aggregate operation may denote an element/key-specific semantic location, while an operation whose meaning concerns aggregate-wide state may denote an aggregate-wide location or a set of locations. The language therefore does **not** promise that operations on distinct keys or elements of the same aggregate conflict or are ordered merely because they belong to that aggregate; a program may rely on such ordering only when the executions access a common semantic location or some other Moss ordering rule establishes it. Semantic locations are not required to coincide with the compiler's physical lock partition.

For static synchronization analysis, the compiler also constructs a finite set of **analysis leaves** $L_D$. Every potentially executed domain-state access is conservatively mapped to one or more analysis leaves that cover its semantic location. In v0.1 a runtime-indexed aggregate may conservatively map many distinct semantic locations to one analysis leaf such as `entries`; a future compiler may refine that mapping without changing Moss source semantics.

Route edges form a concrete directed acyclic graph. The implementation currently plans over the checked handlers present in each concrete specialization rather than performing a whole-program handler-reachability prune first. Consequently, an unreachable declared handler can conservatively inflate the mutable universe and class partition. Reachability pruning is a semantics-preserving future compiler optimization; the proof below does not rely on it.

A deterministic topological linearization assigns each concrete instance a unique `domain_rank(D)`. If the graph contains edge $D \rightarrow E$, then:

$$
\operatorname{domain\_rank}(D) < \operatorname{domain\_rank}(E).
$$
### 19 State effects and observable effects

For handler $h \in H_D$, static effect analysis derives may-effect sets over the finite analysis leaves:

$$
R_D(h),\; W_D(h),\; C_D(h) \subseteq L_D,
$$

representing analysis leaves that may be read, written, or consumed on any control-flow path. Soundness requires every executed semantic state access to be covered by the corresponding static analysis leaf/effect; the analysis representation may be coarser than the semantic location actually accessed.

The analysis is conservative and currently path-insensitive at the handler-summary level: effects are unioned across branches. If one branch writes a cache entry and another only reads it, the handler may still require exclusive synchronization on every invocation. Likewise, accesses to distinct aggregate elements may map to one coarse analysis leaf in v0.1. These are conservative bounds, not claims about source-level conflict granularity or per-execution minimal locking.

We separately use $O(h)$ for observable non-domain-state effects. $O$ does not drive the v0.1 synchronization partition. Under the Section 8 contract, however, $O(h)$ is part of the whole execution: for each dynamically conflicting same-domain pair, the observed behavior of their whole executions, including $O(h)$, must be equivalent to an order consistent with the same domain conflict-serialization order. Independently executing handlers in different domains still have no global $O(h)$ order unless an explicit synchronous dependency or a relevant same-domain conflict establishes one.

Static effects on an analysis leaf normalize as:

$$
\text{consume} > \text{write} > \text{read} > \text{none}.
$$

`consume` remains distinct from `write` for ownership, even though both require exclusive synchronization.
### 20 Mutable universe and protected reads

Define the exclusive leaf set for a handler:

$$
X_D(h) = W_D(h) \cup C_D(h).
$$

The protected mutable universe is:

$$
X_D^* = \bigcup_{g \in H_D} X_D(g).
$$

Only analysis leaves in $X_D^*$ receive synchronization classes. An analysis leaf outside $X_D^*$ is immutable after publication with respect to the checked handler set and may be read without a runtime domain lock once publication is established.

Protected reads are:

$$
\operatorname{ProtectedRead}_D(h) = R_D(h) \cap X_D^*.
$$

The analysis-leaf lock footprint is:

$$
\operatorname{LockSet}_D(h) = X_D(h) \cup \operatorname{ProtectedRead}_D(h).
$$

### 21 Synchronization modes and class partition

For synchronization, normalized effects map to modes:

$$
\text{consume, write} \mapsto \text{exclusive},\qquad
\text{read} \mapsto \text{shared},\qquad
\text{none} \mapsto \text{none}.
$$

For each analysis leaf $\ell \in X_D^*$, define its synchronization-mode signature over a deterministic ordering of handlers:

$$
\sigma_D(\ell) = \langle m(h_1, \ell), \ldots, m(h_n, \ell) \rangle.
$$

Two analysis leaves belong to the same synchronization class iff their complete signatures are equal. This is a compiler partitioning policy, not source-level semantics or module ABI.

Let $\operatorname{class}_D(\ell)$ map a protected analysis leaf to its class. Then:

$$
\operatorname{ClassSet}_D(h) = \{\operatorname{class}_D(\ell) \mid \ell \in \operatorname{LockSet}_D(h)\}.
$$

For a class in a handler's `ClassSet`, the handler mode is exclusive if the signature is exclusive for that handler, otherwise shared.

#### 21.1 Class count is a cost as well as an opportunity

The signature partition preserves every statically visible distinction in handler synchronization behavior. It does not optimize a workload-dependent contention objective. More classes can expose more independent concurrency, but each acquired class still costs a native lock acquisition. The baseline partition is therefore contention-oblivious: it favors preserving potential independence rather than minimizing lock count under an observed workload. Profile-guided coarsening or alternative class objectives are future optimization questions, not v0.1 semantics.

### 22 Running Account derivation

For the `Account` example:

| Handler | R | W | C |
|---|---|---|---|
| `rename` | - | `display_name` | - |
| `set_risk_limit` | - | `risk_limit` | - |
| `record_fill` | `balance`, `stats`, `risk_limit` | `balance`, `stats` | - |
| `read_stats` | `stats` | - | - |
| `read_config` | `config_value` | - | - |

*Table 3: Representative may-effects for the running Account domain.*

Thus:

$$
X_D^* = \{\text{balance},\; \text{stats},\; \text{risk\_limit},\; \text{display\_name}\}.
$$

`config_value` is outside $X_D^*$, so `read_config` requires no domain synchronization after safe publication.

The signatures are distinct:

| Leaf | Handler modes | Result |
|---|---|---|
| `balance` | `record_fill=EXCLUSIVE` | class A |
| `stats` | `record_fill=EXCLUSIVE`, `read_stats=SHARED` | class B |
| `risk_limit` | `set_risk_limit=EXCLUSIVE`, `record_fill=SHARED` | class C |
| `display_name` | `rename=EXCLUSIVE` | class D |

Therefore `record_fill` acquires A and B exclusively and C shared. `rename` is synchronization-disjoint from `record_fill`; `read_stats` can overlap another `read_stats` but conflicts with `record_fill`.

### 23 Path-insensitive locking: the cache-populate bound

Suppose a cache handler is conceptually:

```moss
fn get(key):
  if !entries.contains(key):
    entries[key] = compute(key)
  reply entries[key]
```

The v0.1 analysis may map the runtime-indexed `entries[key]` locations to one aggregate analysis leaf `entries`. Because the handler may write that leaf, its normalized synchronization mode for the resulting class is exclusive on every invocation, even when the requested key is already present or another invocation addresses a different key. Moss v0.1 deliberately accepts this conservative cost in exchange for a complete precomputed `ClassSet`, no lock upgrade, and simple whole-handler 2PL.

This aggregate coarsening is not source semantics. Phase 15.3 implements bounded class-path precision for an eligible leading top-level conditional: its branch-only classes may be deferred, and rank-forced untouched guards may be cancelled, using typed continuation splitting. Mode path ignorance, aggregate coarsening, and general CFG path precision remain conservative/future work. Any further refinement must preserve Section 8's single conflict-serialization order, whole-execution serializability for every realized dynamic conflict, and the deadlock argument. The paper does not claim v0.1 produces the weakest lock mode, smallest lock set, or finest aggregate partition for each dynamic execution.
### 24 Static-footprint strict two-phase locking

Each handler knows a conservative complete `ClassSet` before the body begins. Baseline handlers acquire it at entry. Phase 15.3 may assign statically proven branch acquisition sites to branch-only classes in an eligible leading conditional; every executed protected access remains covered before access, new acquisitions remain rank-ordered, touched guards stay through reply/completion, and a guard may cancel only when statically untouched. There are no shared-to-exclusive upgrades.

This has the static-footprint property often associated with conservative 2PL: the required lock set is known before body execution. However, Moss acquires locks sequentially rather than atomically preclaiming an all-or-none set. Consequently, local class order remains necessary for deadlock freedom.

**Theorem 24.1 (Whole-execution conflict serializability).** Assume the static state-effect summaries soundly cover every executed semantic state access; accesses to the same domain-owned semantic location are covered by a common analysis leaf; every executed protected access is preceded by its covering class in sufficient mode; every touched acquisition obeys two-phase locking; untouched acquisitions may be cancelled only when no reaching path touched them and no remaining path can access them; every actual Moss-managed acquisition precedes the execution's first observable action; touched classes remain held through reply materialization or completion; and nested messages are synchronous. Then the failure-free completed handler executions on any one domain admit a single conflict-serialization order for domain-owned state. Every dynamically conflicting pair is whole-execution serializable consistently with that same order, in the observational sense of Section 8.

**Argument.** Remove cancelled untouched acquisitions from the logical execution. The remaining touched history is strict 2PL, so standard 2PL supplies an acyclic precedence graph and one serial order for protected state [3]. Let the **lock point** be an execution's final actual Moss-managed acquisition. The observable-action barrier puts that lock point before `message`, `echo`, and every other $O(h)$ action. If two executions dynamically conflict, sound semantic-location coverage maps their access to a common class with incompatible modes. A later conflicting execution cannot acquire that class until the earlier execution releases it at completion; therefore its observable portion cannot pass its own lock point until the earlier conflicting execution has completed. Nested messages are synchronous and reply materialization occurs before completion. Thus the observed behavior is equivalent to whole executions ordered consistently with the same 2PL serialization order. Class rank establishes deadlock freedom, not this serialization argument.

Static may-footprints, coarse aggregate leaves, and class partitioning can make the implementation serialize executions that would not dynamically conflict on their realized semantic locations. That extra ordering is permitted but is not source-level conflict semantics. Conversely, this theorem does not turn the synchronous descendant call tree into a transaction with respect to unrelated third-party executions in descendant domains, nor does it impose a total order on $O(h)$ across independent domains.
### 25 Global lock rank and deadlock freedom

Each synchronization class has a deterministic local `class_rank_D(C)`. Define:

$$
\operatorname{LockRank}(D,C) = \bigl(\operatorname{domain\_rank}(D),\; \operatorname{class\_rank}_D(C)\bigr)
$$

with lexicographic order.

Within a handler, classes are acquired in increasing class rank. Across a nested message, the target lies on an outgoing concrete route edge and therefore has greater domain rank. Parent locks remain held while the child acquires its classes.

**Theorem 25.1 (Deadlock freedom for Moss-managed domain locks).** Under the closed acyclic concrete routing graph, deterministic increasing local class acquisition, no lock upgrades, and nested messages only along declared route edges, Moss-managed domain locks cannot participate in a wait-for cycle.

**Proof sketch.** Every newly acquired Moss-managed lock has strictly greater `LockRank` than every Moss-managed lock still held by the current thread. A deadlock cycle would imply

$$
L_1 < L_2 < \cdots < L_n < L_1,
$$

which is impossible for a strict total order. Within one domain, local class rank is sufficient; across nesting, domain rank is the load-bearing component. The ordering applies to currently held locks, not historical acquisitions, so a caller may return from a higher-ranked sibling and then enter a lower-ranked sibling provided that sibling still ranks above the held ancestor locks.

### 26 Why the concrete graph must be closed

The deadlock proof depends on every legal cross-domain message target appearing in the concrete route graph. That is why domain handles are not first-class values. If a handler could receive a domain handle in a payload and later call it, runtime execution could introduce a lock-acquisition edge that the static topology and rank assignment never saw.

Moss therefore restricts handles to concrete composition bindings, declared route bindings, and message receivers. This is not merely an ergonomic restriction; it is part of the proof boundary.

### 27 Publication and immutable reads

Construction and publication must satisfy a happens-before condition:

> All initialization writes to a concrete domain instance happen-before any concurrent handler execution that may observe that instance.

This formulation is stronger and more precise than saying merely that "construction finishes first." It is the reason leaves outside $X_D^*$ may be read without a domain lock: the compiler has established that checked handlers never mutate them, while the runtime establishes safe publication of their initialized values.

### 28 Ownership, aliases, and domain boundaries

Ordinary calls may preserve caller storage identity according to inferred parameter effects. The compiler checks `read`/`write`/`consume` capabilities and rejects overlapping aliases when one side may write or consume.

Domain boundaries deliberately break ordinary Moss alias identity. A message argument establishes an independent semantic value; so does a reply result. For an eligible synchronous message, the backend may represent that semantic value temporarily with an immutable Rust borrow or typed borrowed view when it can prove observational equivalence and non-escape. This physical borrow does not become a Moss alias and cannot outlive the message call. Replies remain owned results. Cross-domain mutable aliases are not part of the language model.

### 29 Failure model and proof scope

Unexpected failure semantics are not yet a language-level supervision model. v0.1 **fails closed**: the production backend aborts on unexpected handler failure or poisoned synchronization rather than releasing possibly inconsistent protected state and continuing Moss execution.

More specifically, a failing handler does not release or weaken guards protecting state it has modified and then permit another Moss handler to observe those partial protected-state updates before process termination. v0.1 does not provide rollback: nested synchronous messages, `echo`, or other observable effects that completed before the unexpected failure may already have occurred.

All serializability and normal-exit state-validity arguments in this paper are scoped to failure-free completed handlers. The fail-closed rule above is a separate failure-containment property of the v0.1 lowering. Phase 21 is reserved for explicit error propagation, supervision, rollback/recovery choices, and their interaction with synchronization.
### 30 Claims and non-claims

#### Claims

- Safe Rust remains the physical memory-safety substrate for generated production code.
- Moss statically closes concrete domain routing in v0.1.
- Moss derives domain-state synchronization from specialized may-effects rather than user-written locks.
- Failure-free completed executions on one domain admit a single conflict-serialization order for domain-owned state. Every dynamically conflicting pair—defined by actually accessing the same domain-owned semantic location with at least one write/consume—is whole-execution serializable consistently with that order: the observed behavior is equivalent to one in which the whole execution of one precedes the whole execution of the other, including nested synchronous messages, reply materialization, and $O(h)$ effects.
- Moss-managed domain lock deadlock is structurally excluded under the closed-graph/rank assumptions.
- Message/reply boundaries are semantically by value and do not expose cross-domain mutable aliases; eligible synchronous message payloads may be implemented with non-escaping immutable Rust borrows, while replies remain owned values. A guard backing a borrowed protected payload remains held for the complete synchronous call.
- In the v0.1 fail-closed lowering, an unexpectedly failing handler does not release modified protected state and allow another Moss handler to observe those partial protected-state updates before process termination.

#### Non-claims

- Moss does not improve on every safety property of Rust; it is more restrictive and obtains additional guarantees in selected concurrency dimensions.
- Moss does not promise global sequential consistency across the entire domain graph.
- Moss does not provide one transaction spanning descendant domains or arbitrary external effects.
- Moss does not guarantee fairness, starvation freedom, lock-free progress, or bounded waiting.
- v0.1 does not define a source-level concurrent-ingress mechanism.
- v0.1 does not yet define supervision, rollback, restart, or recovery after unexpected handler failure; already-completed nested messages or observable effects are not rolled back by fail-closed abort.
- The deadlock proof covers Moss-managed locks, not arbitrary future foreign locks acquired invisibly by external code.
- Static may-effect footprints, analysis-leaf boundaries, `ClassSet` overlap, synchronization-class boundaries, aggregate coarsening, and the current full-handler hold policy are not the source-level definition of handler conflict; they may conservatively serialize executions that do not dynamically conflict.
- The synchronization partition is not guaranteed workload-optimal, path-minimal, or aggregate-element-minimal.
## Part III - Implementation Design, Corrections, and Measurements

### 31 Compilation pipeline

The final v0.1 pipeline is conceptually:

```text
Moss source
    |
    v
parse + check + specialize callables/types
    |
    v
infer read/write/consume + validate call capabilities
    |
    v
build closed ConcreteDomainGraph + exact specializations
    |
    v
derive SynchronizationPlan
    |
    v
generate typed Rust class state + direct RwLock acquisitions
```

The design deliberately keeps semantic artifacts separate from physical lowering. Module interfaces carry checked semantic information; the final application generates its own synchronization partition and physical Rust layout.

### 32 Final production lowering: locks own their protected state

The final backend emits one typed Rust state structure per synchronization class. A representative shape is:

```rust
struct AccountClass0 { balance: i64 }
struct AccountClass1 { stats: Stats }
struct AccountClass2 { risk_limit: i64 }
struct AccountClass3 { display_name: String }
struct AccountImmutable { config_value: i64 }

struct AccountRuntime {
    class0: RwLock<AccountClass0>,
    class1: RwLock<AccountClass1>,
    class2: RwLock<AccountClass2>,
    class3: RwLock<AccountClass3>,
    immutable: AccountImmutable,
}
```

A handler emits direct, statically known acquisitions. There is no runtime handler-plan interpretation:

```rust
let mut c0 = moss_write_or_abort(&self.state.class0);
let mut c1 = moss_write_or_abort(&self.state.class1);
let c2 = moss_read_or_abort(&self.state.class2);

let mut view = RecordFillView {
    balance: &mut c0.balance,
    stats: &mut c1.stats,
    risk_limit: &c2.risk_limit,
};

let result = __moss_body_record_fill(&mut view, fill);
result
```

Rust RAII retains the guards for the handler scope. Nested messages run while parent guards remain live.

### 33 Why the first generic runtime was rejected

The first correct 2PL backend interpreted synchronization metadata at runtime using handler descriptors, class/leaf maps, guard maps, and generic take/restore scaffolding. It was safe and useful while the architecture was changing, but Phase 10.6F measured avoidable fixed overhead on tiny handlers.

That overhead was classified as a v0.1 blocker because the compiler already knew every class, mode, and leaf statically. Phase 10.6F.1 replaced runtime plan interpretation with static typed lowering. The result is an important implementation lesson: the compiler should execute the planning work at compile time and emit ordinary direct Rust, not carry a generic synchronization interpreter into the production hot path.

### 34 Borrowed protected reads and zero-copy message lowering

One correction removed hidden deep copies of protected READ state. Protected reads borrow directly from the data owned by the retained shared guard; immutable-after-publication reads borrow directly from immutable storage. Whole and nested objects are represented with typed borrowed views when needed.

The same principle now applies to eligible synchronous inter-domain message payloads.

At the Moss level, a message still establishes a by-value immutable snapshot. At the Rust lowering level, however, the compiler may represent that snapshot with a temporary immutable borrow when all required conditions are statically known:

- the message call is synchronous;
- the receiver only has immutable, non-consuming access to the incoming payload;
- the source storage remains valid and stable for the complete call;
- if the source storage is protected domain state, the guard protecting that storage remains held for the complete call;
- the borrow cannot escape, be stored, or become a Moss-visible alias; and
- the target is an internal lowering where the compiler controls both sides of the call.

For an ordinary owned value, this can lower to an `&T`. For protected or decomposed domain state, the compiler can reuse the typed borrowed-view machinery already used for protected READs. Small Rust `Copy` values may continue to pass directly by value.

Nested forwarding can remain zero-copy as well. If domain A sends a payload to B and B synchronously forwards that incoming payload to C, the backend may propagate the same immutable borrow through the nested call chain as long as its lifetime remains statically contained.

The optimization is deliberately conservative. If the compiler cannot prove the borrow representation safe—for example at a foreign/native boundary, for an unstable temporary, or where an owned representation is otherwise required—it materializes an owned value.

Replies are not borrowed across the handler boundary. A caller must be able to use a reply after the callee has completed and released its guards, so reply lowering produces an owned semantic result.

The language-level distinction is therefore:

> ordinary READ => borrow where possible; synchronous message => by-value semantics, borrow physically where safe; reply => owned value.

This optimization introduces no Moss reference syntax, no user-visible lifetimes, and no cross-domain mutable aliases. Retaining a guard that backs a borrowed protected payload is a lowering invariant even if a future synchronization optimizer otherwise permits earlier release of unrelated guards. Safe Rust remains responsible for validating the generated physical borrow relationships.

### 35 From asynchronous actors to synchronous domains

The design began with actor-like mailboxes, queues, worker loops, await, sender FIFO, and a per-domain total commit-order concept. That machinery became unnecessary once shared-memory message was made synchronous.

The final model removed await, mailboxes, queues, worker threads, sender FIFO semantics, commit-order machinery, alternate atomic-domain lowering, batching, coalescing, and domain-cluster execution. This simplification sharpened the actual abstraction: a domain is protected state with statically known routing, not a promise of independent scheduling.

### 36 Closing the domain-handle universe

Static routing was initially incomplete because domain handles could still behave like values in some paths. Phase 10.6B.1 closed that hole: handles cannot cross message/reply, ordinary parameters, aggregates, or mutable state. Every legal cross-domain call is now statically attributable to a route edge.

This implementation correction was proof-relevant rather than cosmetic. Without it, domain_rank would not describe the actual nested lock-acquisition graph.

### 37 Fast Debug as a semantic engine

Fast Debug interprets the already-checked program. It constructs logical concrete domain instances, follows checked routes, executes messages as nested interpreter frames, mutates logical state, and terminates on reply. It intentionally does not simulate `RwLock`s, contention, or thread interleavings.

This creates a useful separation:

| | Production | Fast Debug |
|---|---|---|
| Semantics | Checked Moss semantics | Same checked Moss semantics |
| Concurrency | May receive concurrent handler entry | One deterministic legal schedule |
| Locks | Physical compiler-derived `RwLock`s | None |
| Purpose | Native execution | Fast semantic debugging/tracing |

### 38 Performance validation: codegen overhead, not a scalability claim

The strongest v0.1 performance evidence answers a narrow implementation question: after static typed lowering, does a known Moss lock footprint cost materially more than equivalent handwritten Rust using the same `std::sync::RwLock`? On the Phase 10.6F.1 environment, the answer was no for the one-to-three-lock microcases.

| Case | Typed Moss (ns/call) | Hand Rust (ns/call) | Ratio |
|---|---:|---:|---:|
| 1 SHARED | 11.782 | 11.652 | 1.011 |
| 1 EXCLUSIVE | 11.647 | 11.613 | 1.003 |
| 2 EXCLUSIVE | 20.947 | 20.301 | 1.032 |
| Mixed 2-class | 21.639 | 21.171 | 1.022 |
| 3-class | 30.120 | 29.382 | 1.025 |

*Table 4: Phase 10.6F.1 tight-loop medians. These validate codegen overhead, not application scalability.*

The measurements used the same `RwLock` primitive and equivalent logical work. Optimized assembly inspection found no map operations, string comparisons, descriptor loops, heap allocator calls, or `Arc` refcount increments on the protected hot paths. The remaining small differences are consistent with ordinary call/codegen and fail-closed machinery rather than synchronization-plan interpretation [14].

#### 38.1 What the concurrency measurements do not prove

The broader threaded measurements were run on an Intel i3-1315U environment with mixed core types and scheduler/frequency noise. They are useful sanity checks—shared readers overlapped, disjoint writer classes overlapped, conflicting writers serialized—but they are not evidence of general scalability.

A submission-quality scalability study should use uniform physical cores, affinity/pinning, repeated distributions, and workloads that isolate class partitioning from ordinary code-generation effects.

The current paper therefore treats the threaded measurements as implementation validation rather than a headline performance result.

### 39 Class count, nested hold time, and other measured costs

More synchronization classes expose more potential parallelism but also mean more native lock acquisitions. The v0.1 signature partition is intentionally workload-agnostic. Dogfooding may reveal domains where class coarsening would outperform maximal static separation under realistic contention.

Nested synchronous messages also lengthen ancestor critical sections because the v0.1 lowering keeps parent guards held while descendants run. Instrumented Phase 10 measurements found descendant time dominating ancestor hold time in a representative nested sample. That behavior is expected under the baseline proof and is documented rather than hidden.

The full static footprint, aggregate coarsening, and full-handler hold are implementation/proof choices, not source-level conflict semantics. **Phase 15.3 adopts branch deferral and untouched cancellation:** a leading conditional may lower to typed continuation arms, branch-only classes acquire after the condition when rank permits, and rank-forced guards that the chosen arm proves untouched are cancelled. The compiler hoists potential loop acquisitions to the preheader and hoists unresolved classes above a prior observable action. Touched guards still remain through completion; aggregate refinement and early touched release remain future work.

That imposes constraints on **both sides** of an observable effect. Deferred acquisition of a guard that may later establish a dynamic conflict cannot cross an earlier nested `message`, `echo`, or other irreversible observable action whose externally observed position would need to be consistent with that execution's serialization position; if the relevant path is still unresolved, the optimizer must conservatively establish the necessary ordering before the observable action. Symmetrically, releasing or downgrading a touched guard before a later nested message or observable effect can expose behavior that is not equivalent to any whole-execution order consistent with the domain's serialization order.

The optimizer must also preserve the ordinary **two-phase rule** explicitly: once any touched guard is released or downgraded, that handler execution may perform no later Moss-managed guard acquisition or mode upgrade. Releasing or downgrading the first touched guard therefore begins the shrinking phase. By contrast, a guard known to be untouched on the realized execution may be **cancelled at any point**. Such cancellation does not begin the shrinking phase and does not itself forbid later acquisitions, provided the remaining rank-order and observable-effect constraints are still satisfied.

Early release must also preserve Section 29's fail-closed property. A touched guard may not be released while later code can still fail under the modeled Moss failure semantics if doing so could allow another handler to observe a partial update that the v0.1 full-hold lowering would keep hidden until process termination. A guard that backs a borrowed protected message payload must in all cases remain held for the complete dynamic extent of that synchronous call. None of these early-release, deferred-acquisition, aggregate-refinement, or path-precision optimizations are part of v0.1.
### 40 Modules: semantic ABI, physical lowering at the consumer

Source-free providers export semantic information needed for downstream specialization and effect analysis. The final consumer builds the concrete graph, derives synchronization, and emits typed lock-owned state. Physical classes and Rust lifetimes are deliberately absent from the semantic module ABI.

This architecture matters because it lets the final application specialize imported behavior without forcing a provider to predict the consumer's concrete domain instances or lock partition.

### 41 Reachability and other conservative bounds

The current synchronization plan includes checked handlers in the concrete specialization even if whole-program analysis could prove some are unreachable. Such a handler can enlarge $X_D^*$ and split/introduce synchronization classes unnecessarily. This is a straightforward optimization opportunity: prune unreachable handlers before constructing the partition, while preserving diagnostics for declared but unused code.

Other conservative bounds include path-insensitive may-effects and coarse treatment of runtime-indexed aggregates. These are engineering tradeoffs in v0.1, not weaknesses in the deadlock argument.

### 42 Design evolution summary

| Stage | Earlier idea | v0.1 resolution |
|---|---|---|
| Domain execution | Mailbox, queue, worker, await | Synchronous direct message |
| Ordering | Per-domain total commit order | Single same-domain conflict-serialization order; dynamically conflicting pairs ordered as wholes; no graph-wide total order |
| Topology | Handles could still flow dynamically | Closed static concrete routing graph |
| Synchronization | Coarse/alternate backends | Compiler-derived classes + handler-level 2PL |
| READ lowering | Hidden snapshots/clones | Borrowed protected views |
| Physical runtime | Generic maps/descriptors | Static typed `RwLock<ClassState>` lowering |
| Traits | Potential nominal membership | Structural compile-time predicates; no `implements` |
| Parameter mutation | Considered explicit modifiers | Inferred READ/WRITE/CONSUME effects |
| Debugging | Native debugger required for domains | Deterministic Fast Debug semantic engine |
| Language growth | Continue adding features | Phase 15 dogfooding before expansion |

### 43 Dogfooding before expansion

Phase 10 defines the Moss v0.1 usable-language milestone. The next milestone is Phase 15: dogfooding. The goal is to write real programs and let concrete friction drive subsequent work.

Already-known questions include ordinary recursion, module/package ergonomics, trace slicing, source-free generic ergonomics, domain lifetime scopes, and standard-library gaps. None should be solved merely because the roadmap has room. The language should now earn its next features through use.

## Part IV - Related Work and Positioning

### 44 Synchronization inference and lock allocation

Compiler-assisted lock inference is direct prior art. Autolocker infers synchronization for atomic sections [5]. Emmi et al.'s lock allocation infers lock assignments and instrumentation intended to preserve atomicity and deadlock freedom [6]. Cherem, Chilimbi, and Gulwani infer locks for atomic sections using program analysis [7].

Moss should therefore not claim novelty merely because the compiler derives locks. The distinction is the surrounding language contract. Moss does not start from arbitrary shared-memory code annotated with atomic regions. It starts from statically composed source-level domains, closes the concrete instance routing graph, derives effects after specialization, partitions domain-owned state from exact concrete handler signatures, and uses the same instance graph to establish inter-domain lock rank. Domain handles cannot escape and create unseen runtime edges. In that sense, the synchronization inference is one component of a deliberately restricted language rather than a retrofit onto unrestricted shared memory.

### 45 Effect systems and Deterministic Parallel Java

Effect systems provide a long history of static reasoning about program behavior [4]. Deterministic Parallel Java (DPJ) uses a type-and-effect system to provide strong compile-time guarantees for deterministic parallel programming [8]. Moss shares the idea that effects can turn concurrency properties into compile-time facts, but pursues a different user model: structural specialization, source-level state-owning domains, compiler-derived lock partitioning, and static concrete routing rather than programmer-managed regions as the primary architecture.

The important comparison is not “effects versus no effects,” but how much of the concurrency structure the language makes statically recoverable from ordinary source.

### 46 Actors and capability-based race freedom

Actors traditionally isolate mutable state behind asynchronous message processing [10, 11]. Pony's deny capabilities show how a carefully designed capability system can support race-free actor programming with strong static properties [9].

Moss deliberately diverges from classical actor scheduling. A Moss domain is not a mailbox/worker abstraction, messages are synchronous, and multiple compatible handlers on one domain may overlap under compiler-derived shared-memory synchronization. The common idea is to make ownership boundaries explicit enough for the language to reason about concurrency; the execution model is different.

### 47 Rust and Julia as complementary influences

Rust supplies the physical safety substrate and a reference point for systems-programming guarantees [2]. Moss's goal is not to weaken Rust's model, but to make a narrower concurrency discipline easier to use by removing lock selection and order from application source.

Julia supplies a different influence: concise source, specialization, and the idea that high-level generic code need not imply slow generic execution [1]. Moss borrows that design attitude while statically closing behavior that Julia may leave to runtime dispatch. The resulting combination is intentionally asymmetric: Julia-like ergonomic ambition at the source level, Rust-backed safety/native execution at the implementation level, and additional compile-time restrictions around Moss-managed concurrency.

### 48 Open questions

The v0.1 paper leaves several questions deliberately open:

- whether ordinary recursion should be introduced once specialization termination and diagnostics are designed;
- how lexical domain scopes should compose with outer routing anchors and repeated activations;
- whether profile-guided synchronization-class coarsening is worthwhile;
- how to add concurrent ingress without corrupting the static domain model;
- how Rust interoperability should constrain foreign aliases and foreign lock acquisition;
- how supervision/restart semantics interact with state validity and synchronous message chains;
- which module/package improvements are justified by Phase 15 dogfooding.

### 49 Conclusion

Moss's central wager is that a language can feel lighter by making the compiler responsible for facts programmers usually restate manually. The surface aims for Julia-like economy: structural constraints, specialization, and aggressive inference. The implementation deliberately leans on Rust rather than rebuilding a memory-safe native backend. And the concurrency model narrows the space further so that domains, routes, effects, synchronization classes, and lock order are statically established before code generation.

The result is not “Rust but safer” in every dimension, nor “Julia but static.” It is a deliberately restricted point in the design space: high-level source, static closure, safe-Rust lowering, and compiler-owned synchronization. The v0.1 implementation experience suggests that the restrictions are strong enough to support useful proofs and that those proofs need not require an expensive runtime abstraction: after static typed lowering, one-to-three-lock handler wrappers measured in the same cost class as equivalent handwritten Rust.

The next test is not another architecture phase. It is whether Moss is pleasant enough to use on real programs that its restrictions feel like leverage rather than friction.

## Appendix A - Compact v0.1 semantic summary

| Area | v0.1 rule |
|---|---|
| Typing | Static specialization; untyped parameters are statically specialized duck typing |
| Traits | Structural compile-time predicates; no nominal `implements` requirement |
| Callable dispatch | Statically resolved; no runtime dynamic dispatch |
| General closures | Not first-class in v0.1; restricted specialized functional captures exist |
| Ordinary recursion | Disallowed in v0.1 for implementation/specialization reasons, not the synchronization proof |
| Ownership effects | `read`/`write`/`consume` inferred and call-site capability checked |
| Domains | Static state-owning architectural/synchronization units |
| Routes | Immutable `domainroutes`; concrete graph closed and acyclic |
| Domain handles | Routing capabilities, not ordinary values |
| Message | Synchronous, blocking, expression-valued |
| Reply | Terminating, by-value semantic result |
| Payloads | Incoming snapshots are semantically by value and immutable; eligible synchronous message payloads may lower to non-escaping Rust borrows/views; a guard backing protected borrowed storage remains held for the complete call; replies remain owned values |
| Consistency | Failure-free completed executions on one domain admit one conflict-serialization order for domain-owned state; a dynamically conflicting pair (same actually accessed semantic location, at least one write/consume) is whole-execution serializable consistently with that order, meaning its observed behavior is equivalent to one whole execution preceding the other; nonconflicting executions may overlap; no global SC across independent domains |
| External effects | Nested messages, reply materialization, and $O(h)$ participate in the observational whole-execution serializability of dynamically conflicting same-domain handlers according to the same domain conflict-serialization order; nonconflicting/independent executions have no global total order |
| Synchronization | Compiler-derived finite analysis leaves, static may-footprints, and classes; semantic locations define conflict, while v0.1 may conservatively map many semantic locations (for example aggregate keys) to one analysis leaf |
| Locking | v0.1 uses static-footprint strict 2PL, no upgrades, and full-handler hold as a conservative implementation/proof strategy |
| Deadlock order | Lexicographic `(domain_rank, class_rank)` for currently held Moss locks |
| Physical backend | Static typed safe Rust, `RwLock<ClassState>` per synchronization class, borrowed protected READs, and borrowed synchronous message payloads where safe |
| Fast Debug | Deterministic semantic interpreter; no lock/thread simulation |
| Ingress | Source-level concurrent root creation intentionally deferred |
| Failure | Unexpected failure is fail-closed: modified protected state is not released for observation by another Moss handler before process termination; prior nested/observable effects are not rolled back; supervision deferred |

## Appendix B - Proof assumptions checklist

The formal claims rely on the following conditions:

1. Every executed domain-state semantic access is conservatively covered by the compiler's finite analysis-leaf representation, and accesses to the same semantic location are covered by a common analysis leaf.
2. Static effect analysis conservatively contains every relevant handler state read/write/consume over those analysis leaves, including specialized helper and capture effects.
3. Concrete domain routes are closed; legal messages cannot introduce unseen targets.
4. The concrete route graph is acyclic and deterministically topologically ranked.
5. Every protected analysis leaf belongs to exactly one synchronization class derived from its complete handler-mode signature.
6. Every executed protected-state access is preceded by its covering class in sufficient mode. New acquisitions increase Moss global rank and occur before that execution's first observable action; static `ClassSet` members not reached may remain unacquired unless rank, loop, mode, or barrier constraints hoist them.
7. No shared-to-exclusive upgrade occurs.
8. Every touched handler class remains held through terminating reply materialization or normal completion; an earlier cancellation is permitted only for a class proven untouched on all reaching paths and absent from all remaining paths. Consequently parent guards remain held across nested synchronous messages and $O(h)$ effects.
9. Nested messages are synchronous: a descendant handler completes before its caller continues.
10. Every nested message follows a route to a greater domain rank.
11. Initialization writes happen-before concurrent handler execution observes the domain instance.
12. Message/reply value boundaries do not leak Moss-visible mutable aliases between domains; any physical Rust borrow used to lower a synchronous message is immutable, non-escaping, and bounded by the call. If such a borrow is backed by protected state, its protecting guard remains held for the complete call.
13. Normal handler exit leaves every domain-owned value ownership-valid.
14. Serializability claims are for failure-free completed handlers. For unexpected failure, the v0.1 fail-closed lowering does not release or weaken guards protecting modified state and then permit another Moss handler to observe those partial protected-state updates before process termination; already-completed nested messages or observable effects are not rolled back.
15. Foreign code does not acquire hidden Moss-state aliases or violate Moss-managed lock-order assumptions; detailed interop remains future work.
## Appendix C - Moss v0.1 milestone map

| Milestone | Result |
|---|---|
| Phase 10 | Moss v0.1 usable-language milestone |
| 10.5 | Semantic convergence, stable identities, effects/introspection groundwork |
| 10.6A | Synchronous message, terminating reply, `await` retired |
| 10.6B | Static composition, `domainroutes`, concrete DAG, domain rank |
| 10.6B.1 | Closed handle universe and exact concrete specialization linkage |
| 10.6C | Compiler-owned `SynchronizationPlan` |
| 10.6D | Production handler-level 2PL |
| 10.6D.1 | Borrowed protected reads; hidden READ snapshots removed |
| 10.6E | Legacy runtime retired; Fast Debug domains aligned |
| 10.6F | Diagnostics, metrics, implementation validation |
| 10.6F.1 | Static typed synchronization lowering; runtime plan interpretation removed |
| Phase 15 | Dogfooding: write real Moss programs before speculative expansion |
| Phase 20 | Rust interoperability |
| Phase 21 | Error propagation and supervision |
| Phase 22 | Agent agency tooling |

## Appendix D - References

[1] J. Bezanson, A. Edelman, S. Karpinski, and V. B. Shah. Julia: A Fresh Approach to Numerical Computing. SIAM Review, 59(1):65–98, 2017. https://doi.org/10.1137/141000671.

[2] R. Jung, J.-H. Jourdan, R. Krebbers, and D. Dreyer. RustBelt: Securing the Foundations of the Rust Programming Language. Proceedings of POPL, 2018.

[3] K. P. Eswaran, J. N. Gray, R. A. Lorie, and I. L. Traiger. The notions of consistency and predicate locks in a database system. Communications of the ACM, 19(11):624–633, 1976.

[4] J. M. Lucassen and D. K. Gifford. Polymorphic effect systems. Proceedings of POPL, pages 47–57, 1988.

[5] B. McCloskey, F. Zhou, D. Gay, and E. A. Brewer. Autolocker: Synchronization Inference for Atomic Sections. Proceedings of POPL, 2006.

[6] M. Emmi, J. Fischer, R. Jhala, and R. Majumdar. Lock Allocation. Proceedings of POPL, 2007. https://doi.org/10.1145/1190215.1190260.

[7] S. Cherem, T. M. Chilimbi, and S. Gulwani. Inferring Locks for Atomic Sections. Proceedings of PLDI, pages 304–315, 2008. https://doi.org/10.1145/1375581.1375619.

[8] R. L. Bocchino Jr. et al. A Type and Effect System for Deterministic Parallel Java. Proceedings of OOPSLA, pages 97–116, 2009. https://doi.org/10.1145/1640089.1640097.

[9] S. Clebsch, S. Drossopoulou, S. Blessing, and A. McNeil. Deny Capabilities for Safe, Fast Actors. Proceedings of AGERE! 2015, 2015.

[10] C. Hewitt, P. Bishop, and R. Steiger. A Universal Modular ACTOR Formalism for Artificial Intelligence. Proceedings of IJCAI, pages 235–245, 1973.

[11] G. A. Agha. Actors: A Model of Concurrent Computation in Distributed Systems. MIT Press, 1986.

[12] Chapel Project. Chapel language documentation: arrays and domains. https://chapel-lang.org/.

[13] Moss Project. Moss v0.1 implementation and documentation, 2026. https://github.com/kuttyb/moss.

[14] Moss Project. Phase 10.6F.1 static typed synchronization validation, 2026. https://github.com/kuttyb/moss/blob/main/docs/PERFORMANCE_10_6F_1.md.
