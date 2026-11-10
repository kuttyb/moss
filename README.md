# Moss

**Reads like Python. Compiles to Rust. Safe Concurrency by construction.**

Moss is a Python-like language that compiles to Rust. Rust made memory safety the
compiler's job. Moss makes concurrency safety the compiler's job as well.

In most languages, concurrency correctness depends on the programmer: knowing which
data is shared, guarding it with the right lock, taking locks in the right order, and
keeping all of that true as the code changes. Moss takes those decisions away from the
programmer. You describe who owns which state. The compiler generates fine-grained,
deadlock-free locks for you, and proves the program free of data races.

> **Types like a dynamic language. Runs like a static one.**
> Write it like Python, iterate on it like Python, ship it as Rust.

**Built for AI coding.** Agents live in the edit–compile–test loop, and Moss keeps that
loop short. Checks are fast and incremental, so an agent validates every edit in
moments instead of waiting on a full build. Fast Debug, a tracing interpreter, runs the
program instantly with no Rust compile, even while unrelated code is still broken, and
records exactly what happened so an agent debugs from facts instead of guesses.

> **Status:** experimental (v0.1; the v0.9 ownership release is in progress). Linux, macOS and Windows WSL.

---

## Types like a dynamic language, runs like a static one

```moss
type Circle:
  r: Float

  fn area():
    return 3.14159 * r * r

type Square:
  side: Float

  fn area():
    return side * side

fn report(shape):              # no types: anything with an area() works
  return shape.area()

fn main():
  echo report(Circle(r = 1.0))
  echo report(Square(side = 2.0))

  ages = Map()                 # Map[String, Int], inferred
  ages["ada"] = 36
  ages["alan"] = 41
  echo ["ada", "alan"] |> map(ages[_]) |> sum
```

It reads like a script: no `self`, no type parameters, no lifetimes, no annotations.
It compiles like a systems language: every untyped function is specialized for the
concrete types that reach it, so the generated Rust has direct calls, no vtables and
no runtime type checks.

The development loop feels dynamic too:

- **Fast Debug.** A built-in interpreter runs the checked program immediately, with no
  Rust build in the way, and is held to the same results as the native binary. Step
  through it, or record a structured trace of what happened.
- **Run it while it's half-written.** Fast Debug only needs the code your run actually
  reaches to be correct. A broken function you haven't wired in yet doesn't stop you
  from trying the part that works. The release build still rejects every error.
- **Fast checks.** Each package is checked against the compiled interfaces of its
  dependencies instead of rechecking the world, keeping the edit–check–test loop short
  as projects grow.
- **A compiler agents can talk to.** Diagnostics come as structured data with suggested
  repairs, and everything the compiler knows (types, ownership, effects, locks,
  optimization decisions) can be queried directly. Moss measures this on a fixed
  benchmark of fresh-agent coding tasks.

---

## Concurrency the compiler can vouch for

A **domain** is an actor: private mutable state, reached only through its handlers.
Nothing else in a Moss program is shared, so there is nothing else to race on.

```moss
domain Account:
  balance = 0
  owner = "nobody"
  deposits = 0

  fn Deposit(amount: Int):
    balance = balance + amount
    deposits = deposits + 1

  fn Rename(name: String):
    owner = copy(name)         # payloads are lent, not given: keeping one is explicit

  fn Balance() -> Int:
    reply balance

fn main():
  acct = Account()
  executor = Executor().threads(2).start()
  executor.invoke(acct.Deposit(50))      # two independent executions, in parallel
  executor.invoke(acct.Rename("ada"))
  executor.join()
  echo message acct.Balance()            # synchronous call: 50
```

There is no lock anywhere in that program, so there is no lock to forget, misplace or
take in the wrong order. The compiler reads what each handler touches and derives the
synchronization:

| Lock | Protects | `Deposit` | `Rename` | `Balance` |
|---|---|---|---|---|
| 0 | `balance` | exclusive | — | shared |
| 1 | `deposits` | exclusive | — | — |
| 2 | `owner` | — | exclusive | — |

What that buys you:

- **No data races.** Every access to shared state is covered by a lock the compiler
  chose from the code itself. You can't miss one.
- **No lock-order deadlocks.** Locks are ranked and always taken in rank order, and the
  graph of which domains call which must be acyclic. A cycle is a compile-time error
  that names the connections involved, not a hang in production.
- **Safe *and* concurrent.** `Deposit` and `Rename` touch disjoint state, so they get
  disjoint locks and run in parallel on the same actor. A classic actor mailbox would
  have serialized them; a hand-written single mutex would too.
- **Stays correct as the code changes.** `balance` and `deposits` have separate locks
  only because `Balance` reads one and not the other. Remove that reader and they merge.
  Add a handler and the plan is re-derived. Nobody has to remember to update it.
- **Ownership across the boundary.** A synchronous `message` lends its arguments
  read-only: the sender keeps them, nothing is copied, and the handler can't modify
  them. Keeping a payload requires an explicit `copy(...)`, as above. Forgetting it is
  a compile error, not a silent clone or a dangling reference.

The output is ordinary safe Rust: one reader/writer lock per derived class, taken
directly, with no runtime scheduler, message queue or futures.

---

## Functional and eager, optimized past the eager boundary

Pipelines are eager in the language: each stage logically finishes before the next one
starts. That keeps them easy to reason about and to debug. The optimizer then asks a
different question: *could anyone observe the difference?*

```moss
fn normalize(x: Int) -> Int = x * 2

fn main():
  readings = [3, -1, 4, -1, 5, 9]
  scaled = readings |> map(normalize)
  total  = scaled |> filter(_ > 0) |> map(_ + 10) |> sum
  hits   = readings |> filter(_ > 3) |> count
  peak   = readings |> any(_ > 8)
  echo total, hits, peak
```

Read eagerly, that is four traversals and three intermediate vectors. Optimized, it is
two loops and no allocations:

```text
total        one loop: normalize → filter → +10 → sum, straight through 'scaled'
hits, peak   one shared loop over readings; 'any' stops evaluating once true
scaled       still a named value in your code, never built in memory
```

- **It works across statements, not just within a chain.** Named intermediates
  disappear, and independent results over the same data share one pass.
- **It is driven by proofs.** The compiler infers what every callback can do: I/O,
  mutation, messages, failure, non-termination. Stages are fused only when running them
  element by element is indistinguishable from the eager meaning. A missed optimization
  is allowed; an observable reordering is not.
- **It rewrites the computation, not just the loop.** Adjacent maps compose, a `count`
  after a pure `map` skips the map, `any` and `all` stop early when it is safe.
- **The eager meaning is always there.** Unoptimized builds and Fast Debug run every
  stage in order, and every optimization decision can be inspected.

---

## Moss next to Rust

| | Rust | Moss |
|---|---|---|
| Memory safety | borrow checker; you write `&`, `&mut`, lifetimes | same guarantees; ownership inferred, no annotations |
| Data races | prevented by `Send`/`Sync` | prevented: only actor state exists to share |
| Locks | you choose, size and order them | derived, ranked and deadlock-free by construction |
| Generics | traits and type parameters | untyped functions, specialized per call site |
| Iterators | lazy adapters | eager meaning, fused by effect proofs |
| Dev loop | compile, then run | interpret instantly, compile for release |

Moss stands on Rust for everything below the language: the type system it lowers to,
the code generator and the ecosystem. Moss owns the decisions above it.

## What Moss deliberately leaves out

No recursion, no runtime polymorphism, no closures, no shared mutable state outside
domains, no implicit copies of non-primitive values. Each omission buys something the
compiler can then prove for you.

## Work in progress

- **Recursive data: graphs, trees and linked lists.** Moss has no recursive types in
  the core language. Recursive structures are coming through a dedicated Graph DSL, so
  they stay safe by construction rather than reopening the door to aliasing and races.
- **GPU support.**
- **Complex I/O through formally proven protocols.** Interactions with the outside
  world that need more than a simple blocking call will be described as protocols the
  compiler verifies before it generates the code.

## Learn more

- [A Gentle Introduction to Moss](https://github.com/kuttyb/moss/blob/main/docs/GENTLE_INTRODUCTION_TO_MOSS.md), written for Python programmers
- [Language syntax](https://github.com/kuttyb/moss/blob/main/docs/LANGUAGE_SYNTAX.md) and [ownership](https://github.com/kuttyb/moss/blob/main/docs/OWNERSHIP.md)
- [How the compiler derives locks](https://github.com/kuttyb/moss/blob/main/docs/SYNCHRONIZATION_PLAN.md)
- [Functional pipelines and fusion](https://github.com/kuttyb/moss/blob/main/docs/FUNCTIONAL_DATAFLOW.md)
- [Fast Debug](https://github.com/kuttyb/moss/blob/main/docs/FAST_DEBUG.md) and [AI-native development](https://github.com/kuttyb/moss/blob/main/docs/AI_NATIVE_DEVELOPMENT.md)
- [Projects, building and testing](https://github.com/kuttyb/moss/blob/main/docs/PROJECT_WORKFLOW.md)
