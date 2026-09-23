# Phase 15 swarm findings ledger

This is the canonical cross-experiment ledger for Moss friction and defects
found while translating bounded programs under `examples/swarm/`. Individual
experiment READMEs retain their detailed local observations.

## Summary

- Distinct findings: 39
- Open: 10
- Fixed: 28
- Not-a-bug / agent misunderstanding: 1
- Independently reproduced by multiple experiments: 10

(Counts recomputed on 2026-09-23 from the entries below; SWARM-016 was never
allocated, and SWARM-026–030 had not yet been reflected in these totals.)

Completed swarm experiments:

- [Julia / BinaryHeap](Julia/BinaryHeap/)
- [Python / BinaryHeap](Python/BinaryHeap/)
- [Python / Counter](Python/Counter/)
- [Julia / Accumulator](Julia/Accumulator/)
- [Python / HashMap](Python/HashMap/)
- [Julia / DisjointSets](Julia/disjoint_sets/)
- [Python / ChainMap](Python/chain_map/)
- [Julia / FenwickTree](Julia/FenwickTree/)
- [Python / deque](Python/deque/)

## Updating this ledger

Each new `examples/swarm/...` experiment should:

1. Compare its observations against existing `SWARM-*` entries.
2. Increment the observation count and add the experiment when it reproduces an
   existing finding.
3. Allocate the next monotonic ID only for a genuinely distinct finding.
4. Preserve the smallest known reproducer.
5. Distinguish Moss defects from source-language translation difficulty and
   agent misunderstanding.
6. Never renumber existing IDs.

Count distinct findings, not occurrences. A future independent reproduction adds
an observation; it does not create another finding ID.

The HashMap experiment is a Moss open-addressing collection experiment placed in
the Python swarm area. It is not recorded as a source-faithful or line-by-line
translation of a pinned CPython `dict` implementation.

## SWARM-001 — Vector count result fails in arithmetic

- Status: Fixed
- Category: Compiler / type inference
- First observed: [Julia / BinaryHeap](Julia/BinaryHeap/)
- Also observed: [Python / BinaryHeap](Python/BinaryHeap/)
- Observation count: 2

Fix commit: `50ff4b7` (`Fix SWARM-001 parenthesized pipeline inference`)

Regression: `tests/swarm_001_parenthesized_pipeline.moss`, invoked by
`tests/run.sh`.

### Minimal reproducer

```moss
return (data |> count) + 1
```

### Observed behavior

`TYPE_INFERENCE_FAILED` when the `count` result participates in the required
arithmetic expression.

### Workaround

```moss
type BinaryHeap:
  data: Vector[Int]
  size: Int
```

Maintain `size` explicitly beside mutations.

### Notes

This does not mean `count` is universally unusable; the observed limitation is
its result in this arithmetic composition.

## SWARM-002 — User method named `pop` mislowers as collection pop

- Status: Fixed
- Category: Compiler / lowering or name resolution
- First observed: [Julia / BinaryHeap](Julia/BinaryHeap/)
- Also observed: [Python / BinaryHeap](Python/BinaryHeap/)
- Observation count: 2

Fix commit: `b0d17e4` (`Fix SWARM-002 typed pop lowering`)

Regression: `tests/swarm_002_typed_pop.moss`, invoked by `tests/run.sh`.

### Minimal reproducer

```moss
type BinaryHeap:
  fn pop() -> Int:
    return 0

value = heap.pop()
```

### Observed behavior

The source checks, but native lowering attempts a built-in collection-style
`pop_front` operation on `BinaryHeap`.

### Workaround

```moss
fn take_min() -> Int:
  ...
```

This is a naming workaround only.

### Notes

The observation is from native lowering, not source checking.

## SWARM-003 — Unary negative assertion operand confuses formatter

- Status: Fixed
- Category: Tooling / formatter
- First observed: [Julia / BinaryHeap](Julia/BinaryHeap/)
- Also observed: [Python / BinaryHeap](Python/BinaryHeap/),
  [Python / Counter](Python/Counter/)
- Observation count: 3

Fix commit: `814df22` (`Fix SWARM-003 unary-minus formatting`)

Regression: `tests/tooling/check_swarm_003_formatter.py`, invoked by
`tests/run.sh`.

### Minimal reproducer

```moss
assertEqual(value, -4)
```

### Observed behavior

The checker accepts the expression, but formatting reports that assertion
operand types cannot be inferred.

### Workaround

```moss
assertEqual(value, 0 - 4)
```

### Notes

The workaround changes only the expected test expression; heap inputs remained
ordinary negative literals.

## SWARM-004 — Direct comparison of two indexed Vector reads fails

- Status: Fixed
- Category: Compiler / expression or indexing inference
- First observed: [Python / BinaryHeap](Python/BinaryHeap/)
- Also observed: —
- Observation count: 1

Fix commit: `85b00d6` (`Fix SWARM-004 indexed binary checking`)

Regression: `tests/swarm_004_indexed_binary.moss`, invoked by `tests/run.sh`.

### Minimal reproducer

```moss
if data[right] <= data[child]:
  ...
```

### Observed behavior

`MOSS_COMPILE_ERROR: value is not an indexable container`. The semantic type
query reports the same problematic location.

### Workaround

```moss
right_value = data[right]
child_value = data[child]

if right_value <= child_value:
  ...
```

### Notes

The ledger does not assert a compiler root cause beyond the observed error.

## SWARM-005 — Direct return of helper call fails inference

- Status: Fixed
- Category: Compiler / type inference
- First observed: [Python / BinaryHeap](Python/BinaryHeap/)
- Also observed: [Python / Counter](Python/Counter/)
- Observation count: 2

Fix commit: `6718255` (`Fix SWARM-005 implicit method return inference`)

Regression: `tests/swarm_005_implicit_method_return.moss`, invoked by
`tests/run.sh`.

### Minimal reproducer

```moss
return sift_down(start, current)
```

### Observed behavior

`TYPE_INFERENCE_FAILED`.

### Workaround

```moss
sift_down(start, current)
return current
```

### Notes

This was valid for the Python heap because the helper mutation was the important
effect and `current` was already known. Do not generalize beyond that case.

## SWARM-006 — Empty Map construction ignores declared field specialization

- Status: Fixed
- Category: Compiler / type inference
- First observed: [Python / Counter](Python/Counter/)
- Also observed: —
- Observation count: 1

Fix commit: `629757b` (`Fix SWARM-006 empty Map contextual typing`)

Regression: `tests/swarm_006_empty_map_context.moss`, invoked by
`tests/run.sh`.

### Minimal reproducer

```moss
type Counter:
  counts: Map[String, Int]

fn empty_counter() -> Counter:
  return Counter(counts: Map())
```

### Observed behavior

The compiler reports conflicting inferred types `map[string,int]` and `map`.
The explicitly declared field specialization does not establish the empty
constructor's key and value types.

### Workaround

Create a local `Map()`, establish its type with one inert indexed assignment,
then pass that typed local to the object constructor.

### Notes

The seed is not a substitute for Counter's missing-key behavior. It only makes
the otherwise concrete Map construction checkable.

## SWARM-007 — Map write borrows a String key instead of owning it

- Status: Fixed
- Category: Compiler / lowering
- First observed: [Python / Counter](Python/Counter/)
- Also observed: —
- Observation count: 1

Fix commit: `b099e42` (`Fix SWARM-007 Map String key lowering`)

Regression: `tests/swarm_007_map_string_key.moss`, invoked by `tests/run.sh`.

### Minimal reproducer

```moss
type Counter:
  counts: Map[String, Int]

  fn set(key: String):
    counts[key] = 1
```

### Observed behavior

The source checks, but native Rust lowering passes `&String` to map insertion,
which requires an owned `String`.

### Workaround

```moss
stored_key = "" + key
counts[stored_key] = 1
```

### Notes

The value-producing string expression gives the backend an owned key. This is
a compiler-lowering mismatch, not an intended Counter behavior.

## SWARM-008 — Map lacks a missing-key/default lookup surface

- Status: Fixed
- Category: Missing standard collection primitive
- First observed: [Python / Counter](Python/Counter/)
- Also observed: [Julia / Accumulator](Julia/Accumulator/)
- Observation count: 2

Fix commit: `2d787ef` (`Changes`; adds `Map.get(key, default)`).

Regression: `tests/swarm_008_map_default_lookup.moss` plus typed-negative
cases, invoked by `tests/run.sh`.

### Minimal reproducer

```moss
return counts[key]
```

### Observed behavior

A missing map key reaches the native index operation and fails with `no entry
found for key`. `counts.contains(key)` is rejected as an invalid collection
operation, and `counts.get(key, 0)` cannot infer its return type.

### Workaround

None within the experiment's constraints. Pre-populating all behavioral keys
would hide the required Counter operation and was deliberately not used.

### Notes

This blocks the required `get("missing") == 0` behavior. The entry records a
collection-surface gap, not a claim about the intended design of all maps.

## SWARM-009 — Counter method reuse was an agent misunderstanding

- Status: Agent misunderstanding
- Category: Agent misunderstanding
- First observed: [Python / Counter](Python/Counter/)
- Also observed: —
- Observation count: 1

### Minimal reproducer

```moss
type Counter:
  fn increment_by(key: String, amount: Int) -> Int:
    return 0

  fn increment(key: String) -> Int:
    increment_by(key, 1)
    return 0
```

### Verification

The minimal String-argument reproduction checks. An all-`Int` equivalent also
checks, as does the Counter's `increment_by(key, 1)` statement call. Existing
BinaryHeap methods already supplied independent examples of valid implicit
method calls.

### Original draft response

The initial Counter draft duplicated the small read-modify-write body after an
intermediate diagnostic. That duplication is not required by the verified
current source.

### Notes

The original observation conflated an intermediate draft failure with an
implicit method-statement restriction. Returning a helper directly remains the
separate SWARM-005 observation.

## SWARM-010 — Map lacks iterable key/value views

- Status: Fixed
- Category: Missing standard collection primitive
- First observed: [Julia / Accumulator](Julia/Accumulator/)
- Also observed: —
- Observation count: 1

Fix commit: `2d787ef` (`Changes`; adds eager `Map.keys()` / `Map.values()`
snapshots).

Regression: `tests/swarm_010_map_views.moss`, invoked by `tests/run.sh`.

### Minimal reproducer

```moss
for value in data:
  sum = sum + value
```

where `data` is `Map[String, Int]`.

### Observed behavior

The compiler rejects the loop with `MOSS_COMPILE_ERROR`: `type
'map[string,int]' is not statically iterable; expected Vector, range, or
Iterator`. The source-faithful `data |> sum` probe likewise does not produce an
Int value.

### Workaround

None for source-faithful `sum(values(data))` or `merge!` behavior. The Julia
Accumulator control maintains a secondary `total_count` only to test existing-key
mutation; it does not establish Map aggregation.

### Notes

This is distinct from SWARM-008: a default lookup answers one key, whereas this
finding concerns traversal of all Map values (and therefore key/value merge).

## SWARM-011 — Valid sibling-field call lowers to Rust borrow conflict

- Status: Fixed
- Category: Compiler / native lowering
- First observed: PriorityQueue swarm dogfooding
- Also observed: `projects/cache` (sibling-field indexed WRITE)
- Observation count: 2

Fix commit: `7bc1a06` (`Fix dogfood collection parity defects`)

Regression: `tests/swarm_011_sibling_field_borrow.moss`, invoked by
`tests/run.sh` and compiled with `rustc -D warnings`.

### Minimal reproducer

```moss
return update_at(values, size - 1)
```

where `values` requires WRITE access and `size - 1` reads a sibling field of
the same object.

The same lowering class also includes a natural indexed write:

```moss
items[idx + 1] = value
```

### Observed behavior

The Moss checker accepted the call, but native lowering emitted a mutable Rust
borrow for `values` before reading `size`, and rustc rejected it with E0502.

### Workaround

```moss
position = size - 1
return update_at(values, position)
```

### Notes

The native lowering now emits a compiler-generated temporary for a proven pure
Copy-valued sibling read before creating the WRITE borrow. This preserves valid
Moss source evaluation without requiring the programmer to schedule Rust borrows.

## SWARM-012 — `not` expression was not consistently typed as Bool

- Status: Fixed
- Category: Compiler / frontend type checking
- First observed: PriorityQueue swarm dogfooding
- Also observed: —
- Observation count: 1

Fix commit: `4f8462c` (`Changes`)

Regression: `tests/swarm_012_not_bool.moss` and the three typed-negative cases
under `tests/negative/`, invoked by `tests/run.sh`.

### Minimal reproducer

```moss
assert(not ready)
```

### Observed behavior

Native lowering recognized `not`, while frontend inference did not consistently
report the expression as `Bool`; `assert` then rejected a valid condition.

### Workaround

Use an equality comparison such as `assertEqual(ready, false)`.

### Notes

`not Bool -> Bool` now works for nested and grouped Boolean expressions. Numeric
and String operands are rejected with a direct Bool type diagnostic.

## SWARM-013 — Explicit `let` mutation escaped to rustc

- Status: Fixed
- Category: Compiler / frontend ownership checking
- First observed: PriorityQueue swarm dogfooding
- Also observed: —
- Observation count: 1

Fix commit: `4f8462c` (`Changes`)

Regression: `tests/swarm_013_immutable_let_positive.moss` plus direct,
mutating-receiver, member, and indexed negatives under `tests/negative/`, invoked
by `tests/run.sh`.

### Minimal reproducer

```moss
let queue = Queue()
queue.push(1)
```

### Observed behavior

Moss checking accepted the mutation and native Rust later rejected the immutable
binding with E0596.

### Workaround

```moss
var queue = Queue()
queue.push(1)
```

### Notes

The frontend now records explicit `let` bindings and rejects direct, indexed,
member, and mutating-receiver WRITE access with `IMMUTABLE_LOCAL_MUTATION` and
the legal alternative to declare `var` when mutation is intended.

## SWARM-014 — Integer remainder operator unavailable

- Status: Fixed
- Category: Language / arithmetic surface
- First observed: [Python / HashMap](Python/HashMap/)
- Also observed: —
- Observation count: 1

Fix commit: `86208fb` (`Implemented the HashMap dogfooding repairs in the working tree.`)

Regression: `tests/swarm_014_integer_remainder.moss` plus typed-negative cases
under `tests/negative/`, invoked by `tests/run.sh`.

### Minimal reproducer

```moss
idx = mixed % cap
```

### Observed behavior

The original compiler did not recognize `%` as arithmetic, so inference failed.

### Workaround

```moss
idx = mixed - (mixed / cap) * cap
```

### Notes

Moss now supports `Int % Int -> Int`. It follows the existing truncating integer
division model, including signed remainder behavior, and lowers with the matching
wrapping integer operation.

## SWARM-015 — Moss type name collided with Rust backend imports

- Status: Fixed
- Category: Compiler / native symbol hygiene
- First observed: [Python / HashMap](Python/HashMap/)
- Also observed: —
- Observation count: 1

Fix commit: `86208fb` (`Implemented the HashMap dogfooding repairs in the working tree.`)

Regression: `tests/swarm_015_backend_symbol_hygiene.moss`, invoked by
`tests/run.sh` with native Rust `-D warnings` compilation.

### Minimal reproducer

```moss
type HashMap:
  value: Int
```

### Observed behavior

Moss checking succeeded, but generated Rust imported `std::collections::HashMap`
and then emitted the Moss object with the same unqualified name, causing native
name collision errors.

### Workaround

```moss
type IntHashMap:
  ...
```

### Notes

Generated runtime collection and synchronization types are now fully qualified.
The regression also covers legal Moss types named `VecDeque` and `Arc`; the live
HashMap experiment has restored its natural `HashMap` type name.

## SWARM-017 — No natural statically typed empty Vector construction

- Status: Fixed
- Category: Missing language / collection construction primitive
- First observed: [Python / HashMap](Python/HashMap/)
- Also observed: —
- Observation count: 1

Fix commit: `86208fb` (`Implemented the HashMap dogfooding repairs in the working tree.`)

Regression: `tests/swarm_017_typed_empty_vector.moss` and typed-constructor/local
annotation negatives under `tests/negative/`, invoked by `tests/run.sh`.

### Minimal reproducer

```moss
var values = Vector[Int]()
```

### Observed behavior

The original source had no empty typed Vector expression. Attempting a local
annotation such as `var values: Vector[Int] = []` was parsed as a malformed
binding rather than rejected clearly.

### Workaround

```moss
var values = [0]
values.pop()
```

### Notes

`Vector[T]()` is now the built-in empty typed Vector constructor. General local
type annotations remain unsupported and receive
`LOCAL_TYPE_ANNOTATION_UNSUPPORTED` with the constructor as a legal alternative.

## SWARM-018 — Empty Queue construction missed concrete context

- Status: Fixed
- Category: Compiler / frontend type inference
- First observed: `projects/rolling_window`
- Also observed: —
- Observation count: 1

Fix commit: `7bc1a06` (`Fix dogfood collection parity defects`)

Regression: `tests/swarm_018_queue_context.moss` and constructor negatives under
`tests/negative/`, invoked by `tests/run.sh`.

`Queue()` is now specialized by a concrete `Queue[T]` object-field or domain-state
context, matching `Map()` behavior.

## SWARM-019 — Collection pop source contract leaked Rust Option

- Status: Fixed
- Category: Compiler / native lowering and Fast Debug
- First observed: `projects/rolling_window`
- Also observed: —
- Observation count: 1

Fix commit: `7bc1a06` (`Fix dogfood collection parity defects`)

Regression: `tests/swarm_019_collection_pop.moss`,
`tests/swarm_019_empty_pop.moss`, and `tests/swarm_019_empty_queue_pop.moss`.

The frontend's `pop() -> T` contract now extracts `Vec::pop()` / `VecDeque::pop_front()`
through the normal fail-closed path. Empty Vector and Queue pops fail in both native
execution and Fast Debug rather than exposing an Option value.

## SWARM-020 — Typed empty Vector constructor was effect-unresolved

- Status: Fixed
- Category: Compiler / observable-effect analysis
- First observed: `projects/cache`
- Also observed: —
- Observation count: 1

Fix commit: `7bc1a06` (`Fix dogfood collection parity defects`)

Regression: `tests/swarm_020_typed_vector_factory.moss`.

`Vector[T]()` is a pure built-in constructor. Pure helpers may contain normal
local bindings and multiple statements when their observable effects are known.

## SWARM-021 — Fast Debug omitted collection dispatch through object fields

- Status: Fixed
- Category: Fast Debug / interpreter parity
- First observed: `projects/cache` and `projects/rolling_window`
- Also observed: —
- Observation count: 1

Initial fix commit: `7bc1a06` (`Fix dogfood collection parity defects`)

Parity closeout commits: `48db458` (`Fix Fast Debug collection parity`) for
eager defaults and existing Map value/snapshot ownership; `cf3f8c6` (`Close
Fast Debug Map and pipeline parity`) for missing-key non-Copy fallback ownership.

Regression: `tests/swarm_021_field_collections.moss` and
`tests/swarm_021_map_value_semantics.moss` and
`tests/swarm_021_map_get_fallback_owned.moss`.

Fast Debug now executes supported Vector, Queue, and Map operations reached through
checked object fields, including collection indexing and Map `get`, `keys`, and
`values` value/snapshot behavior.

## SWARM-022 — Fast Debug could not initialize concrete Map state

- Status: Fixed
- Category: Fast Debug / interpreter parity
- First observed: `projects/cache` and `projects/rolling_window`
- Also observed: —
- Observation count: 1

Fix commit: `7bc1a06` (`Fix dogfood collection parity defects`)

Regression: `tests/swarm_022_fast_debug_map_state.moss`.

Concrete `Map[Int, Int]` and `Map[String, Int]` state values, including maps nested
inside an ordinary state object, now receive logical empty Map values in Fast Debug.

## SWARM-023 — Fast Debug omitted checked functional pipelines

- Status: Fixed
- Category: Fast Debug / interpreter parity
- First observed: `projects/rolling_window`
- Also observed: —
- Observation count: 1

Initial fix commit: `7bc1a06` (`Fix dogfood collection parity defects`)

Parity closeout commits: `48db458` (`Fix Fast Debug collection parity`) for
eager terminal traversal and typed empty Float sums; `cf3f8c6` (`Close Fast
Debug Map and pipeline parity`) for checked map-output type propagation through
empty pipelines; `49c3e9b9114721a672c0d2b3fef6f908a284a407` (`Use canonical Fast Debug pipeline contexts`) for
canonical function, method, handler, test, and static-specialization lookup.

Regression: `tests/swarm_023_fast_debug_pipelines.moss` and
`tests/swarm_023_eager_terminals.moss` and
`tests/swarm_023_empty_map_types.moss` and
`tests/swarm_023_pipeline_context_identity.moss` and
`tests/swarm_023_pipeline_test_context.moss`.

Fast Debug now executes the current eager `map`, `filter`, `reduce`, `sum`, `count`,
`any`, and `all` pipeline family. Terminal predicates execute in source order even
when their Boolean result is already known; typed empty Float vectors sum to Float zero.

## SWARM-024 — Built-in collection arguments leaked malformed Rust

- Status: Fixed
- Category: Compiler / frontend validation
- First observed: `projects/rolling_window`
- Also observed: —
- Observation count: 1

Fix commit: `7bc1a06` (`Fix dogfood collection parity defects`)

Regression: `tests/negative/swarm_024_queue_constructor_arguments.moss` and
`tests/negative/swarm_024_map_constructor_arguments.moss`.

Unsupported `Queue(items: [])` and `Map(items: [])` are rejected by Moss with a
stable built-in-constructor diagnostic; they are not missing language features.

## SWARM-025 — Fast Debug interpreter omitted unqualified sibling method dispatch on self

- Status: Fixed
- Category: Fast Debug / interpreter parity
- First observed: [Julia / DisjointSets](Julia/disjoint_sets/)
- Also observed: —
- Observation count: 1

Regression: `tests/swarm_025_fast_debug_sibling_method.moss`.

### Minimal reproducer

```moss
type Calculator:
  base: Int

  fn add(x: Int) -> Int:
    return base + x

  fn add_twice(x: Int) -> Int:
    first = add(x)
    return add(first)

fn main():
  c = Calculator(base: 10)
  echo c.add_twice(5)
```

The native compiler lowered unqualified sibling calls within a method body
(such as `add(x)` inside `add_twice`) as method calls on `self`. The Fast Debug
interpreter only searched top-level functions and object constructors, failing
with `unsupported or unresolved callable '<method_name>'`. `FastInterpreter::eval`
now checks the enclosing `self` receiver for matching methods, restoring
parity with native lowering.

## SWARM-026 — Unsupported binary `in` membership expression passed checking

- Status: Fixed
- Category: Compiler / frontend validation
- First observed: [Python / ChainMap](Python/chain_map/)
- Also observed: —
- Observation count: 1

Regression: `tests/negative/swarm_026_in_expression.moss`, invoked by
`tests/run.sh`.

### Minimal reproducer

```moss
fn main():
  var m = Map()
  if "a" in m:
    ...
```

### Observed behavior

The frontend accepted `split_binary` with `" in "`, but Fast Debug rejected
it with `unsupported expression '"a" in m'` and native lowering emitted invalid
Rust `if "a" in m {`.

### Resolution

Binary `in` is not part of the Moss v0.1 expression grammar. `check_expression`
now rejects binary `in` with stable diagnostic `UNSUPPORTED_EXPRESSION_OPERATOR`
and directs users to `Map.get`, key iteration, or an explicit search.

## SWARM-027 — Method call on indexed collection receiver lost element type in native lowering

- Status: Fixed
- Category: Compiler / native lowering & type propagation
- First observed: [Python / ChainMap](Python/chain_map/)
- Also observed: —
- Observation count: 1

Regression: `tests/swarm_027_indexed_collection_method.moss`, invoked by
`tests/run.sh`.

### Minimal reproducer

```moss
var val = maps[0].get("a", 0)
var keys = maps[i].keys()
```

### Observed behavior

The checker accepted the operation, but native lowering's `generated_expr_type`
did not recognize `parse_index` expressions. The receiver type was lost, causing
lowering to fall back to Rust's 1-argument `HashMap::get` and `.keys()` iterator.

### Resolution

Extended `generated_expr_type` to resolve indexed expressions (`parse_index`),
preserving inner collection types (`Vector[T] -> T`, `Map[K, V] -> V`). Method
dispatch on indexed collection elements now correctly selects Moss collection
semantics across both native lowering and Fast Debug.

## SWARM-028 — Self and sibling method calls emitted owned arguments instead of borrowed references

- Status: Fixed
- Category: Compiler / native code generation & type environment
- First observed: [Python / ChainMap](Python/chain_map/)
- Also observed: —
- Observation count: 1

Regression: `tests/swarm_028_self_method_argument_borrow.moss`, invoked by
`tests/run.sh`.

### Minimal reproducer

```moss
type ChainMap:
  maps: Vector[Map[String, Int]]

  fn helper(m: Map[String, Int]) -> Int:
    return m.get("a", 0)

  fn explicit_self_call(m: Map[String, Int]) -> Int:
    return self.helper(m)

  fn call_with_temporary() -> Int:
    return helper(empty_map())
```

### Observed behavior

In native lowering, `gen_object` did not seed `types["self"] = t.name`, and
expression-level simple call lowering did not resolve sibling method signatures.
Arguments to sibling methods were emitted as owned values rather than borrowed
references (`&m`), causing rustc type-mismatch errors.

### Resolution

Populated `types["self"]` in `gen_object` and `check_objects`, and added
sibling method resolution to simple call lowering so inferred parameter
borrow effects are respected for method inter-calls.

## SWARM-029 — Unsupported nested indexed mutation passed checker and failed in backend

- Status: Fixed
- Category: Compiler / frontend validation
- First observed: [Python / ChainMap](Python/chain_map/)
- Also observed: —
- Observation count: 1

Regression: `tests/negative/swarm_029_nested_indexed_mutation.moss`, invoked by
`tests/run.sh`.

### Minimal reproducer

```moss
fn main():
  var v = [Map()]
  v[0]["key"] = 42
```

### Observed behavior

Nested indexed mutation like `v[i][k] = value` was parsed and passed type
checking, but emitted invalid Rust or caused backend errors.

### Resolution

Confirmed nested indexed assignment is unsupported in Moss v0.1. The checker
now rejects nested index assignments in `check_statement` with stable
diagnostic `UNSUPPORTED_NESTED_INDEX_ASSIGNMENT`, instructing developers to
extract the inner collection to a local variable, update it, and write it back.

## SWARM-030 — String comparison between owned String and borrowed &String failed rustc

- Status: Fixed
- Category: Compiler / backend lowering
- First observed: [Python / ChainMap](Python/chain_map/)
- Also observed: —
- Observation count: 1

Regression: `tests/swarm_030_string_comparison_borrowed.moss`, invoked by
`tests/run.sh`.

### Minimal reproducer

```moss
fn search_key(m: Map[String, Int], target: String) -> Bool:
  var keys = m.keys()
  var i = 0
  while i < (keys |> count):
    if keys[i] == target:
      return true
    i = i + 1
  return false
```

### Observed behavior

In functions taking `String` parameters with inferred `READ` effect, the
parameter was lowered to Rust as `&String`, while `keys[i]` was an owned
`String`. Comparing `keys[i] == target` lowered to `(keys[i]) == (target)`,
which rustc rejected because `PartialEq` is not implemented between `String`
and `&String`.

### Resolution

Comparison lowering in `generated_split_binary` now wraps string operands with
`.as_str()`, enabling clean `&str == &str` equality comparisons. In addition,
`ConditionState` handler lowering now populates borrowed message parameters
so condition branch comparisons lower properly.

## SWARM-031 — Parenthesized subexpression lowers to redundant Rust parentheses

- Status: Open
- Category: Compiler / native lowering
- First observed: [Julia / FenwickTree](Julia/FenwickTree/)
- Also observed: [Python / deque](Python/deque/)
- Observation count: 2

### Minimal reproducer

```moss
fn main():
  a = 12
  b = 2
  c = a * (b + 2)
  echo c
```

```moss
fn wrap(a: Int) -> Int:
  return (a + 1)
```

### Observed behavior

`moss check` accepts both, and `moss run --interp` prints `48` and `4`. Native
builds (`margo build`, `margo test`) fail with `BUILD_BACKEND_ERROR`, because
the generated Rust keeps the source parentheses around an already
parenthesized method call, and rustc rejects them under `-D unused-parens`:

```text
let mut c = (a).wrapping_mul(((b).wrapping_add(2_i64)));
return ((a).wrapping_add(1_i64));
```

The error contains raw rustc text with no Moss source line mapping.

### Workaround

Bind the parenthesized subexpression to a local first:

```moss
next = b + 2
c = a * next
```

### Notes

Found independently by both experiments. Any parenthesized operand of an
arithmetic call-style lowering, or a parenthesized `return` value, triggers
it; Fenwick hit it in `i % (p * 2)`.

## SWARM-032 — Formatter rejects valid statements whose expression begins with `(`

- Status: Open
- Category: Tooling / formatter
- First observed: [Julia / FenwickTree](Julia/FenwickTree/)
- Also observed: [Python / deque](Python/deque/)
- Observation count: 2

### Minimal reproducer

```moss
fn f(i: Int) -> Int:
  var p = 1
  while (i / p) % 2 == 0:
    p = p * 2
  return p
```

```moss
fn wrap(a: Int, m: Int) -> Int:
  return (a + 1) % m
```

### Observed behavior

`moss check`, Fast Debug, and native all accept both, but `moss fmt` fails:

- `if`/`while` condition beginning with `(`:
  `FORMAT_PARSE_ERROR: indentation jumps more than one block level`
- `return (...) ...`:
  `FORMAT_PARSE_ERROR: unknown local function 'return'`

This appears to parse `keyword (` as a call to `keyword`.

### Workaround

Reorder so the expression does not start with `(` (for example
`while i % (p * 2) == 0:`; beware SWARM-031), or bind to a local first.

### Notes

A different construct from SWARM-003 (unary minus in assertion operands),
though both are formatter/checker disagreements. The formatter should accept
exactly what `moss check` accepts.

## SWARM-033 — Returning a unary-negated name fails type inference

- Status: Open
- Category: Compiler / type inference
- First observed: [Julia / FenwickTree](Julia/FenwickTree/)
- Also observed: —
- Observation count: 1

### Minimal reproducer

```moss
fn negate(v: Int) -> Int:
  return -v

fn main():
  echo negate(5)
```

### Observed behavior

`TYPE_INFERENCE_FAILED: cannot infer the type of this return expression in
function 'negate'`, even though the parameter and result are both annotated
`Int`. The `legal_alternatives` suggestion to add a type annotation cannot
apply.

The failure is specific to the returned expression's type: `y = -x` in `main`
and `s = -v` as an unused local both check. These fail the same way:

```moss
return -v + 0          # still fails
v = 5
return -v              # local, not parameter: still fails
r = -v
return r               # fails: r's type flows from -v
```

### Workaround

```moss
return 0 - v
```

### Notes

Same family as SWARM-005 (return-expression inference), but a distinct
construct. Negative literals (`-4`) are fine.

## SWARM-034 — No-value function ending in a collection `push` fails result inference

- Status: Open
- Category: Compiler / type inference
- First observed: [Python / deque](Python/deque/)
- Also observed: —
- Observation count: 1

### Minimal reproducer

```moss
fn put(values: Vector[Int], x: Int):
  values.push(x)

fn main():
  var v = [1, 2]
  put(v, 3)
  echo v[2]
```

The method form fails the same way:

```moss
type Box2:
  values: Vector[Int]

  fn put(x: Int):
    values.push(x)
```

### Observed behavior

`TYPE_INFERENCE_FAILED: cannot infer the result type of function 'put'`
(method form: `cannot infer return type for method 'Box2.put'`). The
suggested type annotation is not expressible for a no-value function.

The same function checks when the last statement is anything else (`values[0]
= x`, `echo x`, or any statement after the `push`). The tail `push` call
appears to be treated as an implicit result expression.

### Workaround

End the function with a bare `return`:

```moss
fn put(values: Vector[Int], x: Int):
  values.push(x)
  return
```

## SWARM-035 — Nested mutating calls on the same receiver lower to a Rust double borrow

- Status: Open
- Category: Compiler / native lowering
- First observed: [Python / deque](Python/deque/)
- Also observed: —
- Observation count: 1

### Minimal reproducer

```moss
type Box2:
  values: Vector[Int]

  fn take() -> Int:
    return values.pop()

  fn put(x: Int):
    values.push(x)
    return

  fn cycle():
    put(take())

fn main():
  var b = Box2(values: [1, 2])
  b.cycle()
  b.put(b.take())
  echo b.values[0]
```

### Observed behavior

`moss check` accepts it and `moss run --interp` executes it correctly. The
native build fails with rustc E0499 (`cannot borrow *self as mutable more than
once at a time`) on both `self.put(self.take())` and `b.put(b.take())`.

### Workaround

Sequence the inner call into a local first:

```moss
moved = take()
put(moved)
```

### Notes

Related to SWARM-011, whose fix hoists a pure Copy-valued sibling *read*
before the WRITE borrow. Here the argument is itself a WRITE call on the same
receiver. Moss source order is well defined (argument first), so the lowering
should hoist the argument call into a temporary in the same way.

## SWARM-036 — Unsupported binary operators pass checking

- Status: Open
- Category: Compiler / frontend validation
- First observed: [Julia / FenwickTree](Julia/FenwickTree/)
- Also observed: —
- Observation count: 1

### Minimal reproducer

```moss
fn main():
  i = 6
  j = 3
  k = i | j
  echo k
```

### Observed behavior

None of `&`, `|`, `^`, `&&`, `||`, `and`, `or` (on `Int`) are in bootstrap's
`source_surface.operators`, but all pass `moss check`, typed as `_value`.
What happens next depends on the operator:

| Operator on `Int` | `moss check` | Fast Debug | Native |
| --- | --- | --- | --- |
| `&`, `\|`, `^` | ok | `unsupported expression` | runs (Rust pass-through) |
| `&&`, `\|\|`, `and`, `or` | ok | `unsupported expression` | rustc E0308 |
| `<<`, `>>` | `invalid arithmetic expression` | same | same |

With a parenthesized right operand, `&` is instead misreported as a call:
`i & (0 - i)` gives `UNKNOWN_SYMBOL_OR_TYPE: unknown local function 'i &'`.

### Workaround

Express bit manipulation arithmetically (Fenwick computes `i & -i` by doubling
a power of two while it divides `i`).

### Notes

Same class as SWARM-026 (binary `in`): the checker should reject every
operator outside the supported surface with `UNSUPPORTED_EXPRESSION_OPERATOR`.
Whether Moss v0.1 should gain bitwise operators is a separate language-design
question and is not implied by this finding.

## SWARM-037 — Boolean `and`/`or` work natively but not in Fast Debug, and are undocumented

- Status: Open (needs a language-surface decision)
- Category: Language surface / Fast Debug parity
- First observed: [Julia / FenwickTree](Julia/FenwickTree/)
- Also observed: —
- Observation count: 1

### Minimal reproducer

```moss
fn main():
  a = true
  b = false
  if a and b:
    echo 1
  if a or b:
    echo 2
```

### Observed behavior

`moss check` accepts it and native prints `2`. Fast Debug fails with
`interpreter error: unsupported expression 'a and b'`. Neither bootstrap's
`source_surface.operators` nor the Gentle Introduction mentions `and`/`or`
(only `not`). Separately, `if not a or a:` is rejected with `not operand must
have type 'Bool'`, which suggests `not` currently binds more loosely than `or`.

### Resolution needed

Decide whether short-circuit `and`/`or` are part of v0.1. If they are: add
them to `source_surface` and the docs, implement them in Fast Debug, and fix
`not` precedence. If not: reject them in the checker as SWARM-036 would.

## SWARM-038 — `effects` reports a pure loop helper as divergent/unresolved yet it is accepted as a domain initializer

- Status: Open
- Category: Tooling / semantic query consistency
- First observed: [Python / deque](Python/deque/)
- Also observed: —
- Observation count: 1

### Minimal reproducer

```moss
fn zeros(n: Int) -> Vector[Int]:
  var out = Vector[Int]()
  i = 0
  while i < n:
    out.push(0)
    i = i + 1
  return out

domain Ring:
  buf = zeros(4)

  fn Size() -> Int:
    reply buf |> count

fn main():
  ring = Ring()
  echo message ring.Size()
```

### Observed behavior

`moss effects zeros --json` reports `may_diverge: true` and
`unresolved: true`. The documented composition rule says divergent and
unresolved work is rejected in a domain state initializer, yet `moss check`
accepts `buf = zeros(4)`, and it runs correctly (`4`) in Fast Debug.

### Notes

Either the query is over-conservative for a bounded `while` loop, or the
initializer check does not consult the same facts. Agents cannot tell which
answer to trust. The accepted program matches the documented intent ("pure
helper calls are accepted"), so the `effects` output is the likelier defect.

## SWARM-039 — Fast Debug cannot execute `test` blocks

- Status: Open
- Category: Tooling / Fast Debug coverage
- First observed: [Julia / FenwickTree](Julia/FenwickTree/)
- Also observed: [Python / deque](Python/deque/)
- Observation count: 2

### Observed behavior

`moss run --interp`, `moss debug`, and `margo debug` run only `main`.
Bootstrap lists no interpreted test command, and `margo test` always builds
natively. To check test parity in Fast Debug, both agents independently
generated a scratch copy that rewrote each `test "name":` block as a plain
function called from `main`.

### Notes

A `margo test --interp` (or `margo debug --tests`) mode would give test-level
native/Fast Debug parity checks and a faster edit-test loop, especially while
a native lowering defect (SWARM-031, SWARM-035) blocks `margo test`.

## SWARM-040 — Failure/precondition mechanism for user code is undocumented

- Status: Open
- Category: Documentation / discoverability
- First observed: [Julia / FenwickTree](Julia/FenwickTree/)
- Also observed: [Python / deque](Python/deque/)
- Observation count: 2

### Observed behavior

Both source libraries raise on misuse (Julia `throw(ArgumentError)`, Python
`IndexError` from `pop`/indexing). Neither bootstrap, the skills, nor the
Gentle Introduction says how a user function should fail. `fail(...)` and
`panic(...)` are unknown symbols. The deque agent found by probing that
`assert(condition)` is accepted outside `test` blocks and aborts both
natively (exit 101) and in Fast Debug (exit 1), matching the fail-closed
built-in `Vector.pop()` contract from SWARM-019. The Fenwick agent documented
bounds as unchecked preconditions instead. There is also no way to write a
test that expects a failure.

### Notes

If `assert` in ordinary code is the intended v0.1 failure primitive, say so in
`source_surface`, `moss-language`, and the Gentle Introduction. An
expected-failure test form is a separate, optional surface question.
