# A Gentle Introduction to Moss

> Draft for Moss v0.1

## Abstract

Moss is a statically checked, native systems programming language designed to feel much closer to Python than to traditional systems languages.

> **Feels like Python. Runs like Rust. Safe like Rust. Adds concurrency safety by construction.**

Moss combines Python-like syntax and type inference with native compilation, ownership-aware values, and static checking.

For concurrency, Moss uses an actor-like model: a **domain** owns private mutable state and communicates through synchronous, by-value messages. The programmer writes that simple isolation model; the compiler lowers it to efficient shared-memory execution with compiler-derived fine-grained locks, deadlock-safe lock ordering, and zero-copy borrowing where it can preserve the same value semantics.

So Moss source stays small:

```moss
domain Counter:
  value: Int

  fn Add(amount):
    value = value + amount
    reply value
```

There are no user-visible lifetimes, borrow annotations, lock APIs, actor schedulers, or runtime trait objects. Moss tries to put that systems machinery in the compiler rather than in application code.

This introduction starts with ordinary Python-like programming and gradually introduces the features that make Moss suitable for systems programming.

---

## Why a Python Programmer Might Care

Python is excellent at letting you express an idea quickly. Systems programming becomes harder when you also need predictable native performance, static checking, ownership, and shared-memory concurrency.

Moss is aimed at that gap.

A useful first approximation is:

```text
Python-like source ergonomics
        +
static types and ownership
        +
native compilation
        +
compiler-managed concurrency
```

The concurrency part matters especially as programs grow.

A small program can safely manage a few locks by hand. A large program may have many pieces of mutable state and many operations that need overlapping subsets of that state. Keeping those locks fine-grained improves concurrency, but manually maintaining the correct lock set and lock order becomes increasingly difficult.

Moss makes that a compiler problem.

You write:

```moss
domain Account:
  balance: Int

  fn Deposit(amount):
    balance = balance + amount
```

You do **not** write:

```text
acquire balance lock
remember lock ordering
modify balance
release balance lock
```

If a handler needs several protected pieces of state, the compiler determines the required lock set and acquires them in its established order.

The practical idea is simple:

> **You describe who owns mutable state and what each operation does. Moss generates the synchronization.**

That gives Moss a useful concurrency advantage over writing shared-memory Rust by hand: the programmer is not responsible for maintaining lock placement and lock ordering as the program evolves.

This is one of the main reasons Moss is intended for systems programming rather than simply being another Python-like scripting language.

---

## 1. Values and Types

Moss has four basic scalar types:

```text
Int
Float
Bool
String
```

Most of the time, you do not need to write the type:

```moss
fn main():
  count = 10
  ratio = 2.5
  ready = true
  name = "Moss"
```

You can write it when you want to:

```moss
fn main():
  count: Int = 10
```

Moss is statically checked even when you leave the annotation out. Omitting a type does not make the value dynamically typed.

---

## 2. Branches and Loops

`if` and `while` look familiar:

```moss
fn main():
  i = 0

  while i < 5:
    if i < 3:
      echo "small", i
    else:
      echo "large", i

    i = i + 1
```

The colon says that an indented block follows.

A normal collection can also be traversed with `for`:

```moss
fn main():
  values = [10, 20, 30]

  for value in values:
    echo value
```

The native compiler supports `for`. The current Fast Debug interpreter is still smaller than the production execution engine and does not yet execute `for` traversal.

---

## 3. Functions: Start Untyped, Add Types When Useful

A function can be completely inferred:

```moss
fn add(a, b):
  return a + b
```

You can specify only the return type:

```moss
fn add(a, b) -> Int:
  return a + b
```

Or specify everything:

```moss
fn add(a: Int, b: Int) -> Int:
  return a + b
```

Annotations are optional independently. You can add them where they improve the interface without having to annotate the whole function.

An untyped function is still checked statically. For example:

```moss
fn twice(x):
  return x + x

fn main():
  echo twice(10)
  echo twice(2.5)
```

Moss resolves concrete versions for the uses it sees. It does not turn `x` into a dynamically typed runtime value.

---

## 4. Types: Data and Methods

A Moss `type` is the closest equivalent to a small Python class or Julia struct with methods.

```moss
type Counter:
  name: String
  value: Int

  fn increment(amount):
    value = value + amount

  fn current():
    return value
```

Construct it by naming its fields:

```moss
fn main():
  counter = Counter(
    name: "requests",
    value: 0
  )

  counter.increment(5)
  echo counter.current()
```

### There is no explicit `self`

Inside a method:

```moss
fn increment(amount):
  value = value + amount
```

`value` means the `value` field of the object receiving the method call.

Outside the object:

```moss
counter.increment(5)
```

Inside the method:

```moss
value = value + amount
```

Moss keeps the receiver explicit at the call site and implicit inside the method body.

### Construction is deliberately simple

A type declaration gives you field construction directly:

```moss
counter = Counter(
  name: "requests",
  value: 0
)
```

If construction needs logic, use an ordinary factory function:

```moss
fn make_counter(name):
  return Counter(
    name: name,
    value: 0
  )
```

Moss does not currently add a special constructor language on top of this.

### No `private` or `public` member modifiers

Moss does not have per-field or per-method `private`, `protected`, or `public` declarations.

Module exports control the public boundary of a module. Ordinary type members do not add another access-control language inside the type.

---

## 5. Collections

The built-in collection names are:

```text
Vector
Map
Queue
```

### Vector

A vector literal looks like a Python list:

```moss
fn main():
  values = [10, 20, 30]

  echo values[0]
```

Contained types are inferred:

```moss
type Job:
  name: String
  effort: Int

fn main():
  jobs = [
    Job(name: "compile", effort: 6),
    Job(name: "test", effort: 4)
  ]
```

### Map

```moss
fn main():
  scores = Map()

  scores["compile"] = 12
  scores["test"] = 9

  echo scores["compile"]
```

The key and value types are inferred from use.

### Queue

```moss
fn main():
  pending = Queue()

  pending.push("compile")
  pending.push("test")
```

Again, Moss infers the contained type.

---

## 6. Functional Pipelines

Moss supports collection processing directly:

```moss
fn normalize(value):
  return value * 2

fn main():
  values = [1, 2, 3, 4]

  total = values
    |> map(normalize)
    |> filter(_ > 4)
    |> sum

  echo total
```

Common pipeline operations include:

```text
map
filter
reduce
sum
count
any
all
```

The placeholder `_` keeps small expressions small:

```moss
values
  |> map(_ * 2)
  |> filter(_ > 10)
```

For larger logic, use a named function.

### Pipelines are eager in the language

You can think of each operation as happening in order and producing its normal value.

The compiler is free to make the implementation cheaper. For example:

```moss
values
  |> map(normalize)
  |> filter(_ > 4)
  |> map(score)
  |> sum
```

may compile into one loop without allocating the intermediate vectors.

That is an optimization. You do not have to choose between a separate eager API and lazy iterator API to get it.

More pipeline combinators can be added over time without changing this basic model.

---

## 7. Ownership: The Main Difference a Python Programmer Will Notice

Python programmers are accustomed to assignments creating another reference to the same object:

```python
b = a
```

Moss does not silently create general aliases this way.

For simple values:

```moss
fn main():
  a = 10
  b = a

  echo a
  echo b
```

both values remain usable.

For an owned object:

```moss
type Job:
  name: String

fn main():
  a = Job(name: "compile")
  b = a

  echo b.name
```

the assignment can transfer ownership from `a` to `b`.

Using `a` afterward is then an error:

```moss
echo a.name
```

The useful beginner rule is:

> Moss does not silently deep-copy large values, and it does not silently create unrestricted aliases to them.

The compiler keeps track of who owns a value and reports mistakes before the program runs.

You do not write Rust-style lifetime or borrow annotations in Moss source.

---

## 8. Untyped Functions and Structural Traits

Moss lets you start with a very Python-like style:

```moss
fn twice(x):
  return x + x
```

This works with any concrete use for which `+` makes sense.

Methods can be used the same way:

```moss
fn area(value):
  return value.area()
```

If you want to give that requirement a name, use a trait.

```moss
trait Drawable:
  fn area() -> Int
```

Then unrelated types can satisfy it simply by having the required method:

```moss
type Circle:
  radius: Int

  fn area():
    return radius * radius


type Rectangle:
  width: Int
  height: Int

  fn area():
    return width * height
```

A function can require the trait:

```moss
fn print_area(shape: Drawable):
  echo shape.area()
```

And both values work:

```moss
fn main():
  print_area(Circle(radius: 3))
  print_area(Rectangle(width: 4, height: 5))
```

There is no separate:

```text
implements Drawable
```

declaration.

For a Python programmer, the simple distinction is:

```text
fn f(x):
```

means "let the compiler determine what operations this concrete use needs."

While:

```text
fn f(x: Drawable):
```

means "this interface requirement has a name."

---

## 9. Iteration

The everyday case is simple:

```moss
fn main():
  values = [1, 2, 3]

  for value in values:
    echo value
```

Built-in collections support ordinary iteration.

User-defined iteration is also possible through Moss's static iteration contract. You do not need to understand that machinery to use `for` over normal collections, so it is best treated as a later feature when defining your own collection-like types.

---

## 10. Domains: Private Mutable State with Synchronous Messages

Domains are Moss's concurrency model.

If you have seen the **actor model**, the basic idea will feel familiar: an actor owns private state, and the rest of the program communicates with it by sending messages instead of reaching in and changing that state directly.

A Moss domain follows the same basic model:

```text
private mutable state
        +
synchronous messages
        +
by-value message semantics
```

The important rule is that the domain owns its state. Other code interacts with that state only through the domain's handlers.

For example:

```moss
domain Counter:
  value: Int

  fn Add(amount):
    value = value + amount
    reply value
```

`value` belongs to `Counter`. A caller cannot simply reach in and mutate it.

The message model is also intentionally simple: payloads cross the domain boundary **by value**. The receiver sees an immutable value snapshot. It can read that payload or forward it, but it cannot mutate or consume the incoming message value.

So at the Moss language level, you can think of domains as synchronous actors with value messages.

### What the compiler does underneath

The actor-like model is the programming model, not necessarily the physical implementation.

Because Moss knows the complete domain topology and the state each handler reads or writes, the production compiler can lower domains to efficient shared-memory code:

```text
Moss source:
  private state + by-value messages

Compiler lowering:
  shared memory
  fine-grained compiler-generated locks
  compiler-controlled lock ordering
  borrowed / zero-copy payload passing where safe
```

You never write the locks yourself.

The compiler determines which pieces of state need protection, generates the fine-grained synchronization, and acquires locks in a compiler-controlled order so application code cannot introduce lock-order deadlocks.

And where a synchronous by-value message can be represented safely as a temporary read-only borrow, the backend can avoid physically copying the payload while preserving the same Moss value semantics.

For the programmer, the model stays:

> **Private state. Synchronous messages. Values cross the boundary, not shared mutable aliases.**

Create a domain instance and send it a message:

```moss
fn main():
  counter = Counter(value: 0)

  result = message counter.Add(5)
  echo result
```

A `message` is synchronous. The caller waits until the handler completes.

A handler can return a value with `reply`.

The simple mental model is:

> A domain is an actor-like object with private mutable state, but messages are synchronous.

The state belongs to the domain. Other code does not reach in and mutate it directly.

### Domain-to-domain connections are declared explicitly

Suppose checkout needs to talk to a ledger and a journal:

```moss
domain Checkout:
  name: String

  domainroutes(
    ledger: Ledger,
    journal: Journal
  )

  fn Submit(amount):
    message journal.Record(amount)
    reply message ledger.Credit(amount)
```

`name` is ordinary domain-owned state.

`ledger` and `journal` are routes to other domains.

They are different things:

```text
name       state owned by Checkout
ledger     route to another domain
journal    route to another domain
```

### Bootstrap the domain graph in `main`

```moss
fn main():
  ledger = Ledger(balance: 100)
  journal = Journal(count: 0)

  checkout = Checkout(
    name: "main",
    ledger: ledger,
    journal: journal
  )

  echo message checkout.Submit(25)
```

Constructor argument order does not matter because fields and routes are named.

The beginning of `main` establishes the concrete domain instances and their connections. After that, normal execution proceeds.

### Messages have value semantics

At the language level, a payload sent to another domain is an immutable value snapshot.

The receiving handler may read it or forward it, but it may not mutate or consume an incoming message payload.

The production compiler may implement safe synchronous messages more efficiently under the hood, including shared memory and borrowed reads, without changing the source-level value model.

As a Moss programmer, you write the message semantics, not locks.

---

## 11. Modules

The easiest way to think about a Moss module is:

> A module is a namespace that is also a separate compilation unit.

A module can span several `.moss` files.

For example, two files can both begin with:

```moss
module pricing
```

and together contribute to the same logical `pricing` module.

### Export from one module

```moss
module pricing

export fn double(x):
  return x * 2
```

### Import from another

```moss
module app
import pricing

fn main():
  echo pricing.double(21)
```

Imported names stay qualified:

```moss
pricing.double(21)
```

Declarations are private to their module unless exported.

This keeps the basic model small:

```text
module   namespace
export   visible outside the module
import   depend on another module
foo.bar  use an exported name
```

### What compilation produces

An explicitly compiled module produces two important artifacts:

```text
pricing.mossi
libpricing.rlib
```

`pricing.mossi` describes the Moss-facing interface.

`libpricing.rlib` contains the compiled native implementation used during final linking.

Generic Moss code that still needs specialization carries its Moss semantic representation in the interface so a later consumer can produce the concrete version it needs.

You normally do not need to think about either file while writing application code.

---

## 12. Projects

A Moss project has a `moss.toml` file.

A minimal project looks like:

```text
hello/
  moss.toml
  src/
    main.moss
```

For example:

```toml
[project]
name = "hello"
version = "0.1.0"

[build]
source = "src"
```

An explicit-module project might grow into:

```text
hello/
  moss.toml
  src/
    main.moss
    pricing.moss
    storage.moss
```

Those files do not have to correspond one-to-one with modules. Several source files may declare the same module.

Current v0.1 project commands still live under `moss`, for example:

```sh
moss build
moss test
moss debug .
```

A separate Cargo-like project/package driver named **Margo** is planned, but is not part of current v0.1.

---

## 13. Testing

Tests are ordinary top-level Moss declarations.

```moss
fn add(a, b):
  return a + b

test "addition":
  assertEqual(add(2, 3), 5)
```

Run tests with:

```sh
moss test
```

There are two simple assertion forms:

```moss
assert(total > 0)
assertEqual(actual, expected)
```

Failures are reported against Moss source and Moss values rather than forcing you to read generated Rust diagnostics.

---

## 14. Fast Debug

Moss has two ways to execute checked code.

The normal production path generates Rust, invokes `rustc`, and produces native code.

Fast Debug skips that native build and executes the checked Moss program directly:

```sh
moss debug .
```

or:

```sh
moss run --interp program.moss
```

For a project with explicit modules, Fast Debug follows imports and loads the reachable source-module closure.

Conceptually:

```text
Moss source
    |
    v
parse + static checks
    |
    +----------------------+
    |                      |
    v                      v
Fast Debug              Rust generation
interpreter                 |
                             v
                           rustc
                             |
                             v
                        native program
```

This makes Fast Debug useful for quick edit-run-debug cycles.

Add:

```sh
moss debug . --trace
```

to get a structured execution trace.

### Current Fast Debug limits

Fast Debug intentionally runs Moss semantics directly rather than simulating the production locking implementation.

Current v0.1 Fast Debug also does not yet execute every production construct. In particular, functional pipelines and `for` traversal still require the production backend.

A program accepted and executed by Fast Debug has passed Moss's own static checks. Native compilation additionally passes the generated Rust through Rust's type and borrow checker before machine code is produced.

---

## 15. What Moss Intentionally Does Not Make You Learn

Moss v0.1 deliberately keeps several features out of the language surface.

There is no general macro system.

There is no recursion.

There is no `private`/`public` member-access syntax.

There is no special constructor body; use field construction and factories.

There are no user-visible references or lifetime annotations.

There is no general runtime dynamic dispatch or trait-object model.

There are no general escaping closure values.

There is no requirement to write locks around domain state.

These are not all statements about what a compiler could theoretically support. They are choices about keeping the everyday Moss programming model small.

---

## 16. A Small Program Putting the Pieces Together

Here is a compact example using an ordinary type, a function, a collection, branching, and a pipeline:

```moss
type Item:
  name: String
  price: Int

  fn expensive():
    return price > 20


fn discount(item):
  if item.expensive():
    return item.price - 5
  else:
    return item.price


fn main():
  items = [
    Item(name: "book", price: 15),
    Item(name: "keyboard", price: 50),
    Item(name: "cable", price: 10)
  ]

  total = items
    |> map(discount)
    |> sum

  echo "total", total
```

The code is intentionally ordinary.

There are inferred types, but the program is statically checked.

There are objects and methods, but no explicit `self`.

There is a functional pipeline, but you do not choose a separate lazy execution model.

There is native compilation, but you do not write borrow annotations.

That is the basic Moss idea.

---

## 17. The Beginner Mental Model

If you already know Python, the shortest useful way to approach Moss is:

1. Write variables, functions, branches, loops, objects, and collections normally.
2. Leave types out until an annotation improves the interface.
3. Remember that owned objects do not silently alias when assigned.
4. Use traits when you want to give a structural requirement a name.
5. Use pipelines for collection transformations.
6. Use domains when mutable state needs to be isolated and shared safely.
7. Connect domains explicitly with `domainroutes`.
8. Use modules as named namespaces and compilation units.
9. Use `moss test` for tests and `moss debug` for fast interpreted execution.
10. Let the compiler worry about native lowering, synchronization, and the Rust backend.

Moss is meant to let a programmer begin near Python's level of ceremony while retaining a much more static, native, systems-oriented execution model underneath.
