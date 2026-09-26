# Phase 15 swarm findings ledger

This is the canonical cross-experiment ledger for Moss friction and defects
found while translating bounded programs under `examples/swarm/`. Individual
experiment READMEs retain their detailed local observations.

## Summary

- Distinct findings: 65
- Open: 2
- Deferred to Phase 21: 1
- Fixed: 61
- Not-a-bug / agent misunderstanding: 1
- Independently reproduced by multiple experiments: 26

(Counts recomputed from the detailed per-finding statuses on 2026-09-25 after
Phase 15.14 final closeout of SWARM-032, SWARM-039, SWARM-051, SWARM-059, SWARM-060, and SWARM-066.)

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
- [Multi-Domain Swarm / Domain Torture](domain_torture/)
- [Polymorphism / Sort & Search](polymorphism/sort_search/)
- [Polymorphism / Geometry Modules](polymorphism/geometry_modules/)
- [Polymorphism / Collection Pipeline](polymorphism/collection_pipeline/)
- [Polymorphism / Tree Serialization](polymorphism/tree_serialization/)
- [Modules / Algo Chain](modules/algo_chain/)
- [Modules / Calc Interpreter](modules/calc_interpreter/)
- [Modules / Data Pipeline](modules/data_pipeline/)
- [Modules / Domain Services](modules/domain_services/)
- [Modules / Lib and App](modules/lib_and_app/)
- [Modules / Text Toolkit](modules/text_toolkit/)

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

- Status: Fixed
- Category: Compiler / native lowering
- First observed: [Julia / FenwickTree](Julia/FenwickTree/)
- Also observed: [Python / deque](Python/deque/)
- Observation count: 2

Regression: `tests/swarm_031_parenthesized_lowering.moss`, invoked by
`tests/run.sh` and compiled under `rustc -D warnings`.

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

`moss check` and Fast Debug accepted the legal grouped expressions, but native
lowering re-emitted complete source parentheses around an already structured
Rust expression. Arithmetic lowering then introduced its own method-call
parentheses, producing `unused_parens` warnings that became native build
failures under `-D warnings`.

### Resolution

Native expression lowering now removes only complete balanced outer grouping
parentheses at `expr()` entry using the existing quote-aware
`strip_redundant_outer_parentheses` helper. It no longer re-emits those outer
groups as Rust syntax. Parentheses nested inside a larger expression continue
to protect Moss precedence until the recursive lowering call reaches that
subexpression.

The regression covers `a * (b + 2)`, a parenthesized return value, and the
Fenwick-style `i % (p * 2)` shape. Native compilation with
`rustc -D warnings` and Fast Debug execution must both succeed with identical
results.

---

## SWARM-032 — Formatter rejects valid statements whose expression begins with `(`

- Status: Fixed
- Category: Tooling / formatter
- First observed: [Julia / FenwickTree](Julia/FenwickTree/)
- Also observed: [Python / deque](Python/deque/)
- Observation count: 2

### Resolution

Fixed in Phase 15.14. `canonicalize_code_spacing` in `src/moss.cpp` now checks for keyword boundaries (`while`, `if`, `return`, `echo`, `not`) before `(`, preventing keyword-space stripping that caused the formatter to misparse statements as function calls or block level jumps. Verified with idempotence and malformed grouping tests in `tests/tooling/check_swarm_032_formatter_parentheses.py`.

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

### Notes

A different construct from SWARM-003 (unary minus in assertion operands),
though both are formatter/checker disagreements. The formatter accepts
the same legal expression surface as `moss check`.

## SWARM-033 — Returning a unary-negated name fails type inference

- Status: Fixed
- Category: Compiler / type inference
- First observed: [Julia / FenwickTree](Julia/FenwickTree/)
- Also observed: [Multi-Domain Swarm / Linear Pipeline](domain_torture/linear_pipeline/),
  [Modules / Calc Interpreter](modules/calc_interpreter/),
  [Modules / Lib and App](modules/lib_and_app/),
  [Modules / Domain Services](modules/domain_services/)
- Observation count: 5

### Minimal reproducer

```moss
fn negate(v: Int) -> Int:
  return -v

fn main():
  echo negate(5)
```

And in domain handler reply position:

```moss
domain Counter:
  fn Get() -> Int:
    reply -1
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

In domain handlers, `reply -1` is accepted by `moss check`, but rejected by `moss fmt`:
`FORMAT_PARSE_ERROR: cannot infer the type of this reply expression; add an annotation or use a statically typed value`.

### Workaround

```moss
return 0 - v
reply 0 - 1
```

### Notes

Same family as SWARM-005 (return-expression inference), but a distinct
construct. Negative literals (`-4`) in expressions are fine, but unary negation
in `return` and `reply` positions triggers type inference failures during checking or formatting.

### Resolution

Verified/fixed on the current compiler with a dedicated regression in the lost-semantics closeout pass. The checked Moss semantics now survive through the affected inference/lowering/runtime boundary without relying on backend accidents.


## SWARM-034 — No-value function ending in a collection `push` fails result inference

- Status: Fixed
- Category: Compiler / type inference
- First observed: [Python / deque](Python/deque/)
- Also observed: [Modules / Lib and App](modules/lib_and_app/),
  [Modules / Text Toolkit](modules/text_toolkit/),
  [Modules / Algo Chain](modules/algo_chain/)
- Observation count: 4

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

### Resolution

Verified/fixed on the current compiler with a dedicated regression in the lost-semantics closeout pass. The checked Moss semantics now survive through the affected inference/lowering/runtime boundary without relying on backend accidents.


## SWARM-035 — Nested mutating calls on the same receiver lower to a Rust double borrow

- Status: Fixed
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

### Resolution

Fixed in the lost-semantics closeout pass with a focused regression preserving the checked source semantics across this compiler boundary.


## SWARM-036 — Unsupported binary operators pass checking

- Status: Fixed
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

### Resolution

Fixed by `c310b9783d756a88360469f2f8f016ff866bff1c` (`Fix SWARM-036 and SWARM-052 frontend leaks`). Unsupported symbolic and bitwise binary operators are rejected during Moss frontend validation instead of leaking to Fast Debug or generated Rust. Focused negative regressions cover the rejected operator forms.


## SWARM-037 — Boolean `and`/`or` work natively but not in Fast Debug, and are undocumented

- Status: Fixed
- Category: Language surface / Fast Debug parity
- First observed: [Julia / FenwickTree](Julia/FenwickTree/)
- Also observed: [Modules / Data Pipeline](modules/data_pipeline/),
  [Modules / Text Toolkit](modules/text_toolkit/)
- Observation count: 3

Regression: `tests/swarm_037_boolean_operators.moss` plus the
`tests/negative/swarm_037_*_non_bool.moss` cases, invoked by `tests/run.sh`.

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

`moss check` accepted `and`/`or` and native lowering executed them, while
Fast Debug rejected the same checked expressions. The language surface and
canonical docs mentioned only `not`. In addition, checker precedence parsed
`not a or a` incorrectly.

### Resolution

Moss v0.1 now has four Bool-only word operators: `not`, `and`, `xor`, and
`or`. Precedence from highest to lowest is `not`, `and`, `xor`, `or`.
`and` and `or` short-circuit; `xor` evaluates both operands and returns
true exactly when one operand is true.

Checker/type inference, ownership/effect traversal, native lowering, and Fast
Debug now use the same Boolean expression structure. Untyped parameters used
directly as Boolean operands are constrained to `Bool`. Non-Bool operands
produce `TYPE_MISMATCH` before lowering. Bootstrap `source_surface`, the
canonical language docs, and the Moss language agent skill document the same
contract.

## SWARM-038 — `effects` reports a pure loop helper as divergent/unresolved yet it is accepted as a domain initializer

- Status: Fixed
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

### Resolution

Observable effect analysis now recognizes monotonic bounded counter loops (`while`)
as non-divergent under a conservative proof: the bound must be invariant and
side-effect-free, there must be no induction-variable dependency in the bound, and no
statement in the loop body may mutate variables used by the bound. Mutating collection
methods (`pop`, `pop_front`) are marked as `may_fail: true`. Furthermore, domain state field
initializers (`field.init`) are authoritatively validated for side-effect-freedom during
domain checking using the same observable effect analysis.

Regressions: `tests/swarm_038_effects_loop_helper.moss`,
`tests/negative/swarm_038_impure_state_init.moss`,
`tests/negative/swarm_038_divergent_loop_state_init.moss`,
`tests/negative/swarm_038_empty_pop_state_init.moss`, and
`tests/tooling/check_phase15_14_swarm_038_051.py`.

## SWARM-039 — No project-wide Fast Debug test discovery/orchestration

- Status: Fixed
- Category: Tooling / Fast Debug coverage
- First observed: [Julia / FenwickTree](Julia/FenwickTree/)
- Also observed: [Python / deque](Python/deque/)
- Observation count: 2

### Resolution

Fixed in Phase 15.14. `moss test --interp` and `margo test --interp` now provide project-wide Fast Debug test discovery and orchestration across multi-file and multi-module project test suites, supporting test filtering and trace emission (`--trace`) without requiring Rust compilation.

Regression: `tests/tooling/check_swarm_039_project_fast_debug_tests.py`.

### Notes

Project-wide interpreted testing gives test-level native/Fast Debug parity checks and a faster edit-test loop without invoking rustc.

## SWARM-040 — Failure/precondition mechanism for user code is undocumented

- Status: Deferred to Phase 21
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

Phase 15.15 scope decision: defer this failure/precondition specification
question to Phase 21 alongside recoverable error propagation and supervision.

## SWARM-041 — Collection method on struct field during handler effect analysis triggers internal invariant error

- Status: Fixed
- Category: Compiler / effect and synchronization analysis
- First observed: [Multi-Domain Swarm / Rich Payloads](domain_torture/rich_payloads/)
- Also observed: —
- Observation count: 1

### Minimal reproducer

```moss
type Packet:
  scores: Vector[Int]
  attributes: Map[String, Int]

  fn total() -> Int:
    x = scores |> sum
    return x + attributes.get("alpha", 0)

domain Consumer:
  fn Process(p: Packet) -> Int:
    reply p.total()

fn main():
  consumer = Consumer()
  m = Map()
  m["alpha"] = 10
  p = Packet(scores: [1, 2, 3], attributes: m)
  echo message consumer.Process(p)
```

### Observed behavior

`moss check` fails with:
`error[MOSS_INTERNAL_OR_IO_ERROR]: internal synchronization invariant: unresolved concrete method effect target` (at `src/moss.cpp:4658`).

When a functional pipeline (e.g. `scores |> sum`) appears in a method called from a domain handler, `leaf_effect_capture_` is set during effect analysis. A subsequent collection method call such as `attributes.get("alpha", 0)` on a struct field triggers line 4658's assertion:
`synchronization_require(!leaf_effect_capture_, "unresolved concrete method effect target")`
because built-in collection methods on fields are not in `object_method`.

### Workaround

Use strict indexing `attributes[key]` instead of `.get(key, default)`, or compute the collection access before the pipeline:

```moss
val = attributes.get("alpha", 0)
return (scores |> sum) + val
```

### Notes

The effect analyzer should reset `leaf_effect_capture_` after the pipeline stage finishes or treat built-in collection query methods on fields as pure/read effects during capture.

### Resolution

Verified/fixed on the current compiler with a dedicated regression in the lost-semantics closeout pass. The checked Moss semantics now survive through the affected inference/lowering/runtime boundary without relying on backend accidents.


## SWARM-042 — Reassigning an initialized mutable local across all conditional branches triggers rustc `-D unused-assignments`

- Status: Fixed
- Category: Compiler / native lowering
- First observed: [Multi-Domain Swarm / Dispatcher Fan-out](domain_torture/dispatcher_fanout/)
- Also observed: —
- Observation count: 1

### Minimal reproducer

```moss
fn choose(flag: Bool) -> Int:
  var res = 0
  if flag:
    res = 10
  else:
    res = 20
  return res

fn main():
  echo choose(true)
```

### Observed behavior

`moss check` accepts it and `moss run --interp` (Fast Debug) executes cleanly (`10`).
Native compilation (`moss ... -o ...` and `rustc -D warnings` or `margo build`) fails with `BUILD_BACKEND_ERROR` because rustc rejects the lowered Rust under `-D unused-assignments`:

```text
error: value assigned to `res` is never read
  --> test.rs:54:19
   |
54 |     let mut res = 0_i64;
   |                   ^^^^^ this value is reassigned later and never used
...
62 |         res = 20_i64;
   |         ------------ `res` is overwritten here before the previous value is read
   |
   = note: `-D unused-assignments` implied by `-D warnings`
```

### Workaround

Avoid pre-initializing a mutable local if it is unconditionally overwritten across all branches of a conditional. Assign directly to an immutable binding or return directly from each branch:

```moss
fn choose(flag: Bool) -> Int:
  if flag:
    return 10
  else:
    return 20
```

### Notes

Similar class of backend warning leakage as SWARM-031 (`-D unused-parens`). Native lowering emits `let mut res = initial_expr;` even when every control-flow path reassigns `res` before any read, triggering Rust's `-D unused-assignments`. Lowering could emit uninitialized bindings where valid, avoid emitting mut when unneeded, or handle branch convergence.

### Resolution

Verified/fixed on the current compiler with a dedicated regression in the lost-semantics closeout pass. The checked Moss semantics now survive through the affected inference/lowering/runtime boundary without relying on backend accidents.


## SWARM-043 — Untyped parameter field access monomorphizes function to first caller type

- Status: Fixed (Phase 15.15)
- Category: Compiler / type inference & specialization
- First observed: [Polymorphism / Geometry Modules](polymorphism/geometry_modules/)
- Also observed: [Polymorphism / Tree Serialization](polymorphism/tree_serialization/)
- Observation count: 2

### Minimal reproducer

```moss
type Alpha:
  name: String

type Beta:
  name: String

fn get_name(item):
  return item.name

fn main():
  a = Alpha(name: "A")
  b = Beta(name: "B")
  echo get_name(a)
  echo get_name(b)
```

### Observed behavior

`moss check` fails on `get_name(b)` with `TYPE_MISMATCH: expected Alpha, found Beta`.
In `src/moss.cpp:1561-1565`, untyped parameter field access (`item.name`) records a `ConstraintKind::Field` constraint but fails to mark `function.generic = true`. Consequently, the function monomorphizes on its first concrete call (`Alpha`) and permanently freezes its signature as `fn get_name(item: Alpha) -> String`. Calling it with another type having the identical field causes a compile error.

### Workaround

Encapsulate the field behind a method (`item.get_name()`). Method calls on untyped parameters correctly trigger structural trait / generic specialization.

---


### Phase 15.15 resolution

Untyped parameters support inferred static method requirements, but not inferred
field requirements. The checker rejects direct field access, including via an
alias, with `UNTYPED_FIELD_ACCESS` at the function body. Annotate the parameter
with a concrete type or expose shared behavior through a method/trait.

## SWARM-044 — Untyped collection indexing fails checking with container error

- Status: Open
- Category: Compiler / type inference & specialization
- First observed: [Polymorphism / Sort & Search](polymorphism/sort_search/)
- Also observed: [Polymorphism / Geometry Modules](polymorphism/geometry_modules/), [Polymorphism / Collection Pipeline](polymorphism/collection_pipeline/)
- Observation count: 3

### Minimal reproducer

```moss
fn get_first(items):
  return items[0]

fn main():
  v = [10, 20, 30]
  echo get_first(v)
```

### Observed behavior

`moss check` fails with:
`TYPE_ERROR: value is not an indexable container` at `src/moss.cpp:7591`.
Untyped function parameters (`items`) are not inferred as generic container types when indexed in the function body unless annotated or promoted via structural methods.

### Workaround

Wrap container operations in structural traits with `fn get(i: Int)` / `fn set(i: Int, v)`, or pass concrete collections directly.

---

## SWARM-045 — Exported struct fields lower without pub modifier across modules

- Status: Fixed
- Category: Compiler / module projection & native lowering
- First observed: [Polymorphism / Geometry Modules](polymorphism/geometry_modules/)
- Also observed: [Polymorphism / Collection Pipeline](polymorphism/collection_pipeline/),
  [Modules / Data Pipeline](modules/data_pipeline/),
  [Modules / Lib and App](modules/lib_and_app/),
  [Modules / Algo Chain](modules/algo_chain/),
  [Modules / Calc Interpreter](modules/calc_interpreter/),
  [Modules / Domain Services](modules/domain_services/),
  [Modules / Text Toolkit](modules/text_toolkit/)
- Observation count: 8

### Minimal reproducer

```moss
# Module A:
module DataMod
export type Record:
  val: Int

# Module B:
module AppMod
import DataMod
fn inspect(r: DataMod.Record) -> Int:
  return r.val
```

### Observed behavior

`moss check` and Fast Debug accept cross-module field read `r.val` and constructor instantiation `DataMod.Record(val: 10)`.
However, native compilation fails in `rustc` with:
`error[E0616]: field 'val' of struct 'Record' is private` or `error[E0451]: field 'val' of struct 'Record' is private`.
Lowering emits `pub struct Record { val: i64 }` where individual fields lack the Rust `pub` visibility modifier.

### Workaround

Provide explicit exported getter methods (`fn get_val() -> Int: return val`) or constructors within the defining module.

---

### Resolution

Verified/fixed on the current compiler with a dedicated regression in the lost-semantics closeout pass. The checked Moss semantics now survive through the affected inference/lowering/runtime boundary without relying on backend accidents.


## SWARM-046 — Exported trait-annotated function crashes with internal synchronization invariant

- Status: Fixed
- Category: Compiler / module interface projection
- First observed: [Polymorphism / Geometry Modules](polymorphism/geometry_modules/)
- Also observed: [Modules / Data Pipeline](modules/data_pipeline/),
  [Modules / Lib and App](modules/lib_and_app/),
  [Modules / Algo Chain](modules/algo_chain/),
  [Modules / Calc Interpreter](modules/calc_interpreter/),
  [Modules / Domain Services](modules/domain_services/)
- Observation count: 6

### Minimal reproducer

```moss
module Geom
export trait Shape:
  fn area() -> Int

export fn get_area(s: Shape) -> Int:
  return s.area()
```

The same invariant crash is also triggered by non-primitive compound expressions in exported functions:

```moss
# Two method calls in binary op (Data Pipeline repro/two_method_calls):
export fn f(p: a.P) -> Int:
  return p.get_x() + p.get_y()

# Float literal on LHS of multiplication (Lib and App repro/float_literal_left_export):
export fn perimeter(w: Float, h: Float) -> Float:
  return 2.0 * (w + h)

# Binary op of helper calls (Algo Chain repro/export_two_calls):
export fn sum_two(v: Vector[Int], a: Int, b: Int):
  return first(v, a) + first(v, b)
```

### Observed behavior

Compiling the module triggers an internal compiler crash during `.mossi` interface generation:
`error[MOSS_INTERNAL_OR_IO_ERROR]: internal synchronization invariant: unresolved concrete method effect target` at `src/moss.cpp:4658` (or `unresolved callable argument`).
During module export effect analysis, compound expressions in exported functions fail to resolve leaf effect targets when parameters are trait-constrained or when binary operations combine method calls or float literals.

### Workaround

Leave the exported function parameter untyped (`export fn get_area(s) -> Int: return s.area()`), sequence calls into local variables before binary operations (`x = p.get_x(); y = p.get_y(); return x + y`), or put arithmetic literals on the right (`(w + h) * 2.0`).

---

### Resolution

Fixed in the lost-semantics closeout pass with a focused regression preserving the checked source semantics across this compiler boundary.


## SWARM-047 — Compiler assertion abort on pipeline reduce with Map accumulator

- Status: Fixed
- Category: Compiler / functional pipeline lowering
- First observed: [Polymorphism / Collection Pipeline](polymorphism/collection_pipeline/)
- Also observed: —
- Observation count: 1

### Minimal reproducer

```moss
fn record_freq(var counts: Map[Int, Int], item: Int):
  counts[item] = counts.get(item, 0) + 1

fn main():
  items = [1, 2, 2, 3]
  var counts = Map[Int, Int]()
  items |> reduce(counts, record_freq)
```

### Observed behavior

Compiler terminates with SIGABRT:
`Assertion '!node.effects.unresolved && !node.callable_identity.empty()' failed` at `src/moss.cpp:6054`.

### Resolution

The effect walker now recognizes checked built-in Map reads, including `get`,
without marking the callback unresolved. Native lowering also binds a consumed
Map parameter as mutable when its body assigns through an index. The assertion
remains in place. The committed reproducer and `tests/swarm_047_reduce_map.moss`
pass checking, native compilation/execution, and Fast Debug.

Phase 15.14 follow-up: the checker now retains a parameter's WRITE observation
separately when its final inferred effect is CONSUME. Native lowering uses that
checked fact to declare the owned parameter mutable. The regression covers
indexed Map mutation inside `if` and `while`, plus mutation through an ordinary
helper, with native and Fast Debug result parity. SWARM-047 remains fixed.

### Workaround

Use an explicit `while` loop to accumulate into the Map instead of `|> reduce`.

---

## SWARM-048 — Fast Debug interpreter fails to resolve function identifier passed as callable argument

- Status: Fixed
- Category: Fast Debug interpreter / name resolution
- First observed: [Polymorphism / Collection Pipeline](polymorphism/collection_pipeline/)
- Also observed: [Polymorphism / Tree Serialization](polymorphism/tree_serialization/)
- Observation count: 2

### Minimal reproducer

```moss
fn is_positive(x: Int) -> Bool:
  return x > 0

fn filter_ints(items: Vector[Int], pred) -> Vector[Int]:
  var out = Vector[Int]()
  for x in items:
    if pred(x):
      out.push(x)
  return out

fn main():
  nums = [1, -2, 3]
  echo filter_ints(nums, is_positive)
```

### Observed behavior

Compiles and executes natively with zero errors via static monomorphization.
In Fast Debug (`moss run --interp` or `margo debug`), aborts with:
`interpreter error: unknown local 'is_positive'`.
The interpreter evaluates call argument expressions in the local variable environment where function names do not exist.

### Workaround

Use direct inline logic, static pipeline lambdas (`_ > 0`), or execute natively with `margo run`.

---

### Resolution

Fixed in the lost-semantics closeout pass with a focused regression preserving the checked source semantics across this compiler boundary.


## SWARM-049 — Fast Debug string relational comparison evaluates to false

- Status: Fixed
- Category: Language surface / frontend validation
- First observed: [Polymorphism / Sort & Search](polymorphism/sort_search/)
- Also observed: —
- Observation count: 1

Regression: `tests/negative/swarm_049_string_{lt,le,gt,ge}.moss` and
`tests/swarm_049_string_builtin_operators.moss`, invoked by `tests/run.sh`.

### Minimal reproducer

```moss
fn main():
  echo "apple" < "banana"
```

### Observed behavior

Native execution inherited Rust's String ordering and printed `true`, while
Fast Debug coerced non-integer relational operands through its numeric path and
printed `false`. The Moss language contract had not specified whether String
ordering was legal.

### Resolution

Moss v0.1 has a closed compiler-defined operator set and does not support
user-defined operator overloading. String ordering is not part of that set.
The frontend now rejects String `<`, `<=`, `>`, and `>=` with stable
diagnostic `UNSUPPORTED_STRING_ORDERING`, so backend behavior cannot define
the language accidentally.

String `+` remains a built-in concatenation operation, and String
`==`/`!=` remain built-in equality operations. Native and Fast Debug
regressions verify those supported operations agree.

---

## SWARM-050 — Nested generic specialization fails in Rust lowering backend

- Status: Fixed
- Category: Compiler / native lowering & specialization
- First observed: [Polymorphism / Sort & Search](polymorphism/sort_search/)
- Also observed: —
- Observation count: 1

### Minimal reproducer

```moss
trait Sortable:
  fn len() -> Int
  fn less(i: Int, j: Int) -> Bool
  fn swap(i: Int, j: Int)

fn partition(s: Sortable, low: Int, high: Int) -> Int:
  return low

fn quicksort(s: Sortable):
  p = partition(s, 0, 1)

type TypeA:
  fn len() -> Int: return 2
  fn less(i: Int, j: Int) -> Bool: return true
  fn swap(i: Int, j: Int): pass

type TypeB:
  fn len() -> Int: return 2
  fn less(i: Int, j: Int) -> Bool: return true
  fn swap(i: Int, j: Int): pass

fn main():
  var a = TypeA()
  var b = TypeB()
  quicksort(a)
  quicksort(b)
```

### Observed behavior

`moss check` succeeds.
Native compilation fails in `rustc` with `E0308: mismatched types`: the
compiler emits concrete `quicksort` and `partition` specializations for both
types, but the first `quicksort` specialization calls the second `partition`
specialization.

### Resolution

Both inner specializations were emitted, but shared branch join metadata from
the later checked outer specialization replaced the earlier outer parameter's
concrete type during native generation. Lowering now retains the concrete type
already established for each function instance. The committed reproducer and
`tests/swarm_050_nested_specialization.moss` pass native compilation/execution
and Fast Debug; the regression calls two concrete outer and inner pairs.

Phase 15.14 follow-up: each checked static specialization now retains its own
control-flow join environment. Native lowering requires the matching context
for a branch-created local, so later specializations cannot replace its type.
`tests/swarm_050_join_specialization.moss` checks both discovery orders, native
and Fast Debug results, and the exact outer-to-inner generated call pairs.
SWARM-050 remains fixed.

### Workaround

Inline helper logic into the outer generic function, or specialize the helper function explicitly per concrete type.

---

## SWARM-051 — Unqualified sibling callable argument across modules fails native lowering

- Status: Fixed
- Category: Compiler / module name resolution & lowering
- First observed: [Polymorphism / Collection Pipeline](polymorphism/collection_pipeline/)
- Also observed: —
- Observation count: 1

### Minimal reproducer

```moss
module Tools
export fn map_by(items: Vector[Int], transform) -> Vector[Int]:
  var out = Vector[Int]()
  for x in items:
    out.push(transform(x))
  return out

module App
import Tools
fn double_val(x: Int) -> Int:
  return x * 2

fn main():
  nums = [1, 2, 3]
  echo Tools.map_by(nums, double_val)
```

### Observed behavior

Fails in native lowering with `missing static specialization with argument types (vector[int], unresolved)`.
Module function name mangling renames `double_val` to `App__double_val`, but lowering looks up `"double_val"`.

### Resolution

Module expression rewriting now tracks in-scope local variables and parameters with
lexical and control-flow aware scoping per indentation level, preventing parameter
callables from being mangled with module prefixes while properly handling branch/loop
locals that shadow sibling function names, as well as body-local names used by trailing
result expressions. Unqualified sibling callable identifiers and explicitly module-qualified
callable identifiers (`App.double_val`) both correctly rewrite to the canonical
module symbol (`App__double_val`). Fast Debug and static native specialization
now both recognize and execute static callables across module boundaries. Both
unqualified sibling syntax and explicitly qualified syntax converge to the same
canonical semantic target and native behavior, with explicit Fast Debug project coverage.

Regression: `tests/tooling/check_phase15_14_swarm_038_051.py`.

---

## SWARM-052 — Vector[Trait]() passes frontend check but fails native compilation

- Status: Fixed
- Category: Compiler / frontend validation
- First observed: [Polymorphism / Geometry Modules](polymorphism/geometry_modules/)
- Also observed: [Polymorphism / Tree Serialization](polymorphism/tree_serialization/)
- Observation count: 2

### Minimal reproducer

```moss
trait Shape:
  fn area() -> Int

fn main():
  var shapes = Vector[Shape]()
```

### Observed behavior

`moss check` accepts `Vector[Shape]()`.
Native lowering emits `std::vec::Vec::<Shape>::new()`, which fails in `rustc` with `error[E0425]: cannot find type 'Shape' in this scope`.
Moss v0.1 does not support runtime trait objects; collections cannot hold abstract trait types.

### Workaround

Store concrete types in separate typed collections (`Vector[Circle]()`, `Vector[Rectangle]()`) or use dynamic dispatch wrappers. The checker should reject `Vector[<Trait>]` at compile time.

---

### Resolution

Fixed by `c310b9783d756a88360469f2f8f016ff866bff1c` (`Fix SWARM-036 and SWARM-052 frontend leaks`). Trait types are rejected in concrete collection element positions, preserving the v0.1 rule that traits are static constraints rather than runtime trait objects.


## SWARM-053 — Binary string concatenation between owned String and literal fails rustc

- Status: Fixed
- Category: Compiler / native lowering
- First observed: [Polymorphism / Tree Serialization](polymorphism/tree_serialization/)
- Also observed: [Modules / Lib and App](modules/lib_and_app/),
  [Modules / Data Pipeline](modules/data_pipeline/),
  [Modules / Domain Services](modules/domain_services/)
- Observation count: 4

### Minimal reproducer

```moss
fn main():
  var s = "hello"
  s = s + " world"
```

### Observed behavior

Native compilation fails with rustc `error[E0308]: mismatched types: expected &str, found String` because `(s) + (" world")` lowers to `(String) + (String)`.

### Workaround

Use a multi-statement accumulator helper (`var out = ""; out = out + s; out = out + " world"; return out`) or formatted string interpolation where supported.

---

### Resolution

Verified/fixed on the current compiler with a dedicated regression in the lost-semantics closeout pass. The checked Moss semantics now survive through the affected inference/lowering/runtime boundary without relying on backend accidents.


## SWARM-054 — Chained field access on indexed vector in method omits usize cast in backend

- Status: Fixed
- Category: Compiler / native lowering
- First observed: [Polymorphism / Sort & Search](polymorphism/sort_search/)
- Also observed: —
- Observation count: 1

### Minimal reproducer

```moss
type Item:
  score: Int

type Container:
  items: Vector[Item]

  fn get_score(i: Int) -> Int:
    return items[i].score
```

### Observed behavior

Native compilation fails in `rustc` with `error[E0277]: the type [Item] cannot be indexed by i64` because the backend emits `items[i].score` without `(i as usize)`.

### Workaround

Pass `items[i]` to a helper projection function (`fn item_score(it: Item) -> Int: return it.score`), which infers `READ` borrow and correctly casts the index.

---

### Resolution

Verified/fixed on the current compiler with a dedicated regression in the lost-semantics closeout pass. The checked Moss semantics now survive through the affected inference/lowering/runtime boundary without relying on backend accidents.


## SWARM-055 — assertEqual with brace in string literal argument leaks unescaped brace into Rust format string

- Status: Fixed
- Category: Compiler / test lowering
- First observed: [Polymorphism / Tree Serialization](polymorphism/tree_serialization/)
- Also observed: —
- Observation count: 1

### Minimal reproducer

```moss
test "brace test":
  assertEqual("{hello}", "{hello}")
```

### Observed behavior

Native compilation of test binary fails in `rustc` because `{` in the literal argument is interpolated directly into `format!("{hello}")` without escaping to `{{hello}}`.

### Workaround

Bind expected string to a local variable before asserting: `expected = "{hello}"; assertEqual(actual, expected)`.

---

### Resolution

Verified/fixed on the current compiler with a dedicated regression in the lost-semantics closeout pass. The checked Moss semantics now survive through the affected inference/lowering/runtime boundary without relying on backend accidents.


## SWARM-056 — Methods of exported types are omitted from compiled `.mossi` interface metadata

- Status: Fixed
- Category: Compiler / module interface projection
- First observed: [Modules / Data Pipeline](modules/data_pipeline/)
- Also observed: [Modules / Lib and App](modules/lib_and_app/),
  [Modules / Algo Chain](modules/algo_chain/),
  [Modules / Text Toolkit](modules/text_toolkit/),
  [Modules / Domain Services](modules/domain_services/)
- Observation count: 5

### Minimal reproducer

In provider package (`lib`):
```moss
module shapes

export type Box:
  w: Int
  h: Int

  fn area() -> Int:
    return w * h

export fn make_box(w: Int, h: Int) -> Box:
  return Box(w: w, h: h)
```

In consumer package (`app`):
```moss
module app
import shapes

fn main():
  b = shapes.make_box(2, 3)
  echo b.area()
```

Committed paired reproducer: `examples/swarm/modules/data_pipeline/repro/dep_type_methods` (`one_package` vs `two_packages/app`), and `examples/swarm/modules/lib_and_app/repro/method_across_package`.

### Observed behavior

`margo debug` (Fast Debug) succeeds and prints `6` because it parses dependency source directly into AST.
When compiled natively across package boundaries (`margo build` / `margo run`), `moss build` fails in the consumer with:
`error[MOSS_COMPILE_ERROR]: no matching method 'shapes__Box.area' for supplied arguments`.
Inspection of `build/debug/shapes.mossi` reveals that `export type Box nominal` emits only public field representations (`w`, `h`) and completely omits member method definitions (`fn area() -> Int`). Consequently, downstream packages importing `shapes` via `.mossi` perceive `Box` as having zero methods.
Within the same package across module boundaries, the same code compiles and runs cleanly because all package sources share an in-memory compilation context.

### Workaround

Expose top-level exported free-function wrappers in the provider package (`export fn box_area(b: Box) -> Int: return b.area()`), and call `shapes.box_area(b)` downstream.

### Notes

Specifically module/package-boundary-dependent: strictly a cross-package compiled interface (`.mossi`) projection defect. Single-package multi-module code and Fast Debug AST execution succeed.

---

### Resolution

Fixed in the lost-semantics module-closure pass. The checked module/interface semantics are now carried into source-free interfaces and the transitive Rust dependency projection, with the original reproducer retained as regression coverage.


## SWARM-057 — `for i in range(...)` fails type inference when enclosed in an explicit module

- Status: Fixed
- Category: Compiler / module type inference
- First observed: [Modules / Data Pipeline](modules/data_pipeline/)
- Also observed: [Modules / Lib and App](modules/lib_and_app/),
  [Modules / Text Toolkit](modules/text_toolkit/),
  [Modules / Algo Chain](modules/algo_chain/),
  [Modules / Domain Services](modules/domain_services/)
- Observation count: 5

### Minimal reproducer

Single file (passes):
```moss
fn tri() -> Int:
  s = 0
  for i in range(0, 3):
    s = s + i
  return s

fn main():
  echo tri()
```

Split into explicit module (fails):
```moss
module stats

export fn tri() -> Int:
  s = 0
  for i in range(0, 3):
    s = s + i
  return s
```

Committed paired reproducer: `examples/swarm/modules/data_pipeline/repro/range_in_module` (`single` vs `split`).

### Observed behavior

In standalone or single-file non-module source, `for i in range(...)` passes `moss check` and native compilation.
As soon as the containing source includes a `module <name>` header, `moss check` and `margo build` fail with:
`error[TYPE_INFERENCE_FAILED]: cannot infer the static iterator source type`.
Furthermore, in multi-file projects, the error is misattributed to line 5 of `main.moss` rather than the module file defining the loop (see SWARM-059).

### Workaround

Rewrite `for i in range(...)` loops iteratively using `while`:

```moss
i = 0
while i < 3:
  s = s + i
  i = i + 1
```

### Notes

Specifically module/package-boundary-dependent: introducing an explicit `module` boundary breaks otherwise valid `range` type inference.

---

### Resolution

Fixed in the lost-semantics module-closure pass. The checked module/interface semantics are now carried into source-free interfaces and the transitive Rust dependency projection, with the original reproducer retained as regression coverage.


## SWARM-058 — Sibling method call within exported module type mis-mangles as module function

- Status: Fixed
- Category: Compiler / module name resolution & lowering
- First observed: [Modules / Data Pipeline](modules/data_pipeline/)
- Also observed: [Modules / Lib and App](modules/lib_and_app/),
  [Modules / Text Toolkit](modules/text_toolkit/),
  [Modules / Calc Interpreter](modules/calc_interpreter/),
  [Modules / Algo Chain](modules/algo_chain/)
- Observation count: 5

### Minimal reproducer

```moss
module p

export type P:
  x: Int

  fn a() -> Int:
    return x + 1

  fn b() -> Int:
    return a() + 1
```

Committed paired reproducer: `examples/swarm/modules/data_pipeline/repro/sibling_method` (`single` vs `split`).

### Observed behavior

In a single file without `module`, calling a sibling method unqualified (`return a() + 1`) resolves to `self.a()` (verified in SWARM-025 / SWARM-028).
Inside an explicit `module p`, `rewrite_module_program` prefixes unqualified calls with the module name rather than resolving `self` methods, rewriting `a()` into `p__a()`.
Because `a` is a member method and not a top-level function in module `p`, compilation fails with:
`error[TYPE_INFERENCE_FAILED]: cannot infer the type of this return expression in function 'p__P.b'`
or `error[UNKNOWN_SYMBOL_OR_TYPE]: unknown local function 'p__a'`.

### Workaround

Inline the sibling method logic or declare a private top-level helper function inside the module (`fn helper_a(p: P) -> Int: return p.x + 1`).

### Notes

Specifically module/package-boundary-dependent: identical method structure passes in single-file non-module source and fails when placed inside a `module`.

---

### Resolution

Fixed in the lost-semantics closeout pass with a focused regression preserving the checked source semantics across this compiler boundary.


## SWARM-059 — Multi-file project diagnostics misattribute error source file to root or first module

- Status: Fixed
- Category: Tooling / diagnostic source attribution
- First observed: [Modules / Domain Services](modules/domain_services/)
- Also observed: [Modules / Data Pipeline](modules/data_pipeline/),
  [Modules / Lib and App](modules/lib_and_app/),
  [Modules / Calc Interpreter](modules/calc_interpreter/)
- Observation count: 4

### Resolution

Fixed in Phase 15.14. Physical source file provenance is now preserved during parsing, whole-project module composition, type checking, and specialization. Diagnostics and structured JSON errors accurately report the physical source file containing the construct rather than falling back to the project root or first loaded file.

Regression: `tests/tooling/check_swarm_059_diagnostic_provenance.py`.

### Minimal reproducer

File `src/alpha.moss` (4 lines):
```moss
module alpha
export fn ok() -> Int:
  return 1
```

File `src/beta.moss` (9 lines):
```moss
module beta
import alpha

export fn bad(x: Int) -> Int:
  return x + "s"
```

Committed paired reproducer: `examples/swarm/modules/domain_services/repro/diag_file` (`split`).

### Observed behavior (before fix)

`margo build` fails on the type mismatch in `beta.moss`, but reported:
`source_file: .../src/alpha.moss`
even though `alpha.moss` has only 4 lines.
During whole-project merged analysis, diagnostics retained the line offset within the originating module file, but defaulted the `source_file` path to the root module or the first source file loaded in the project.

### Notes

Specifically module/package-boundary-dependent: occurs only in multi-file projects where merged ASTs lose individual file provenance.

---

## SWARM-060 — `moss edit rename` fails on module-qualified entities with `EDIT_TARGET_AMBIGUOUS`

- Status: Fixed
- Category: Tooling / semantic edit
- First observed: [Modules / Domain Services](modules/domain_services/)
- Also observed: [Modules / Text Toolkit](modules/text_toolkit/),
  [Modules / Data Pipeline](modules/data_pipeline/),
  [Modules / Lib and App](modules/lib_and_app/)
- Observation count: 4

### Resolution

Fixed in Phase 15.14. `moss edit rename` and target resolution now handle module-qualified entity selectors (e.g. `mod.fn`, `entity-v1:function:mod__fn`) unambiguously. The rename engine maps module prefixes across qualified and unqualified declarations and call sites without `EDIT_TARGET_AMBIGUOUS`, while genuinely ambiguous unqualified requests continue to be rejected.

Regression: `tests/tooling/check_swarm_060_qualified_rename.py`.

### Minimal reproducer

```moss
# src/util.moss:
module util
export fn double(x: Int) -> Int:
  return x * 2

# src/main.moss:
module app
import util
fn main():
  echo util.double(21)
```

Run:
```sh
moss edit rename entity-v1:function:util__double triple --json
```

Committed paired reproducer: `examples/swarm/modules/domain_services/repro/edit_rename` (`single` vs `split`).

### Observed behavior (before fix)

In single-file code, `moss edit rename entity-v1:function:double triple --json` succeeds and updates declaration and callers.
In multi-module code, `moss inspect util__double` succeeds and reports durable identity `entity-v1:function:util__double`. However, running `moss edit rename` failed with:
`error[EDIT_TARGET_AMBIGUOUS]: rename could not map every semantic reference to one exact token`.

### Notes

Specifically module/package-boundary-dependent: semantic renaming works cleanly for project-local non-module functions.

---

## SWARM-061 — Test binary compilation fails when test modules import internal modules omitted from `main.moss`

- Status: Fixed
- Category: Compiler / test target lowering
- First observed: [Modules / Calc Interpreter](modules/calc_interpreter/)
- Also observed: [Modules / Data Pipeline](modules/data_pipeline/),
  [Modules / Domain Services](modules/domain_services/)
- Observation count: 3

### Minimal reproducer

In `src/main.moss`:
```moss
module app
import util

fn main():
  echo util.double(21)
```

In `src/other.moss`:
```moss
module other
export fn triple(x: Int) -> Int:
  return x * 3
```

In `tests/other_test.moss`:
```moss
module other_test
import other

test "triple":
  assertEqual(other.triple(4), 12)
```

Committed paired reproducer: `examples/swarm/modules/calc_interpreter/repro/test_imports_not_in_main` (`split`), and `data_pipeline/repro/test_module_mismatch`.

### Observed behavior

`margo test` fails during Rust backend compilation of the test binary (`app.rs`):
`error[BUILD_BACKEND_ERROR]: module Rust crate compilation failed for 'app'`
`error[E0425]: cannot find function other__triple in this scope`.
Moss projects all project unit tests into the application's root crate (`app.rs`), but only emits Rust crate imports (`use moss_<mod>::*;`) for modules imported by `main.moss`. Any internal module imported only by a test file is not in scope in the test binary.

### Workaround

Add unused import declarations for all internal project modules to `src/main.moss` (e.g. `import other # needed for tests`).

### Notes

Specifically module/package-boundary-dependent: test projection assumes the root main module imports the union of all modules required by test files.

---

### Resolution

Fixed in the lost-semantics module-closure pass. The checked module/interface semantics are now carried into source-free interfaces and the transitive Rust dependency projection, with the original reproducer retained as regression coverage.


## SWARM-062 — Specialized imported generic function calling transitive module fails native lowering

- Status: Fixed
- Category: Compiler / module specialization & lowering
- First observed: [Modules / Algo Chain](modules/algo_chain/)
- Also observed: —
- Observation count: 1

### Minimal reproducer

```moss
# leaf.moss
module leaf
export fn bump(x: Int) -> Int:
  return x + 1

# mid.moss
module mid
import leaf
export fn scaled_bump(x):
  y = x + x
  return leaf.bump(1)

# main.moss
module app
import mid
fn main():
  echo mid.scaled_bump(10)
```

Committed paired reproducer: `examples/swarm/modules/algo_chain/repro/generic_transitive_import` (`single` vs `split`).

### Observed behavior

In a single compilation unit, `scaled_bump` monomorphizes and runs cleanly (`10`).
When split across modules, `app` imports `mid`, and `mid` imports `leaf`. Monomorphization projects the concrete specialization of `scaled_bump` into `app.rs`.
However, because `app.moss` does not import `leaf`, `app.rs` does not include `use moss_leaf::*;`, causing rustc to fail:
`error[E0425]: cannot find function leaf__bump in this scope`.

### Workaround

Explicitly add a dummy import of the transitive module in the consumer module (`import leaf # generic body needs it linked`).

### Notes

Specifically module/package-boundary-dependent: monomorphization projects specialized generic function bodies into the caller's crate without pulling in the transitive module dependencies of the callee.

---

### Resolution

Fixed in the lost-semantics module-closure pass. The checked module/interface semantics are now carried into source-free interfaces and the transitive Rust dependency projection, with the original reproducer retained as regression coverage.


## SWARM-063 — Transitive struct field types leak unimported Rust trait requirements across modules

- Status: Fixed
- Category: Compiler / native lowering & type generation
- First observed: [Modules / Lib and App](modules/lib_and_app/)
- Also observed: —
- Observation count: 1

### Minimal reproducer

```moss
# inner.moss
module inner
export type In:
  v: Int
export fn mk(v: Int) -> In:
  return In(v: v)

# outer.moss
module outer
import inner
export type Out:
  i: inner.In
export fn mk(v: Int) -> Out:
  return Out(i: inner.mk(v))

# main.moss
module app
import outer
fn main():
  o = outer.mk(4)
```

Committed paired reproducer: `examples/swarm/modules/lib_and_app/repro/transitive_import_closure` (`single` vs `split`).

### Observed behavior

In single-module code, this compiles and runs cleanly.
When split across modules, `app` uses `outer.Out`, which internally has a field of type `inner.In`.
The Rust lowering backend generates view trait implementations for `Out` inside `app.rs`:
`impl<F0: MossAccess_inner__In> std::fmt::Debug for outer__OutView<F0>`
Because `app.moss` only imported `outer` and did not import `inner`, `MossAccess_inner__In` is not in scope in `app.rs`.
Native compilation fails in rustc with:
`error[E0405]: cannot find trait MossAccess_inner__In in this scope`.

### Workaround

Module `app` must explicitly import `inner` (`import inner`), exposing private internal implementation types of `outer` to the consumer.

### Notes

Specifically module/package-boundary-dependent: generated Rust view traits for imported composite types leak transitive module trait requirements into consumer crates.

---

### Resolution

Fixed in the lost-semantics module-closure pass. The checked module/interface semantics are now carried into source-free interfaces and the transitive Rust dependency projection, with the original reproducer retained as regression coverage.


## SWARM-064 — Struct field named with a Rust reserved keyword fails native compilation

- Status: Fixed
- Category: Compiler / native lowering & symbol hygiene
- First observed: [Modules / Lib and App](modules/lib_and_app/)
- Also observed: [Modules / Calc Interpreter](modules/calc_interpreter/)
- Observation count: 2

### Minimal reproducer

```moss
type Holder:
  box: Int

fn main():
  h = Holder(box: 3)
  echo h.box
```

Committed reproducer: `examples/swarm/modules/lib_and_app/repro/rust_keyword_field`.

### Observed behavior

`moss check` succeeds and Fast Debug (`moss run --interp`) executes cleanly.
Native compilation fails in `rustc` with 12 errors:
`error: expected identifier, found reserved keyword box`.
The backend lowers Moss struct fields directly without Rust raw identifier escaping (`r#box`).

### Workaround

Rename the field to avoid Rust reserved keywords (e.g. `bx: Int`).

### Notes

Non-boundary defect (occurs equally in single-file and multi-module programs). Related to SWARM-015 (type name collisions), but affects struct field names.

---

### Resolution

Verified/fixed on the current compiler with a dedicated regression in the lost-semantics closeout pass. The checked Moss semantics now survive through the affected inference/lowering/runtime boundary without relying on backend accidents.


## SWARM-065 — Boolean pipeline (`filter |> any`) fails native lowering with invalid return type

- Status: Fixed
- Category: Compiler / native lowering
- First observed: [Expression Surface / Numerical Tool](expression_surface_2026/numerical_tool/)
- Observation count: 1

### Minimal reproducer

```moss
fn has_large_shift(energies: Vector[Int]):
  return energies |> filter(_ >= 100) |> any(_ > 0)

fn main():
  echo has_large_shift([1, 100])
```

Committed reproducer: `examples/swarm/expression_surface_2026/numerical_tool/failed_attempts/07_boolean_pipeline_native/`.

### Observed behavior

`moss check` passes. Fast Debug (`moss run --interp`) executes correctly (outputs `true`).
Native compilation fails with:

```
error: expected one of `!`, `(`, `+`, `::`, `<`, `where`, or `{`, found `:`
  --> build/debug/boolean_pipeline_native_repro.rs:53:72
   |
53 | ...functional_result:has_large_shift {
   |                     ^ expected one of 7 possible tokens
```

The generated Rust contains the invalid identifier `_functional_result:has_large_shift` as a
return type annotation. The colon is not valid Rust in that position. This is a native lowering
codegen defect: the pipeline stage name leaks into the generated Rust return type using an
invalid name mangling scheme.

### Workaround

Assign the pipeline result to a local variable before returning, or restructure with a
`while` loop. A multi-stage pipeline in a direct `return` statement triggers the defect;
staging through a local is safe.

### Notes

Non-boundary defect: fails equally in single-file programs. Distinct from SWARM-046 (which is
a synchronization invariant crash). The invalid `:` in `_functional_result:has_large_shift`
suggests the stage/function name is being spliced into a Rust type position without proper
hygiene. Confirmed by expression-surface swarm (Phase 15.12A) with minimal reproducer.

---

### Resolution

Verified/fixed on the current compiler with a dedicated regression in the lost-semantics closeout pass. The checked Moss semantics now survive through the affected inference/lowering/runtime boundary without relying on backend accidents.


## SWARM-066 — `for i in range(...)` accepted by checker but native lowering loses induction binding

- Status: Fixed
- Category: Compiler / native lowering
- First observed: [Control Flow / Simulation Engine](control_flow_2026/simulation_engine/)
- Observation count: 1

Regression: `tests/swarm_066_for_range_lowering.moss`, registered in `tests/run.sh`.

### Root cause

In `gen_block` (`src/moss.cpp`), the `Stmt::Kind::For` / `is_range` path emitted:

```rust
for index in <start_expr>..<end_expr> { body }
```

When `<end_expr>` expands to a Rust block (e.g. a functional pipeline
`values |> count` lowered to `{ let __moss_pipeline_source = ...; __moss_result }`),
Rust parses `{ block }` after `..` as the **for-loop body**, not the upper bound.
This created a `RangeFrom` (`start_expr..`) with the pipeline block as the body,
leaving the actual Moss loop body unreachable. The induction variable was never
declared in the loop scope, producing `E0425` and `E0308` from rustc.

### Fix

Range bounds are now always materialized into typed `let __moss_range_start_<line>: i64`
and `let __moss_range_end_<line>: i64` bindings before the for-loop. The for-loop
header then uses simple variable references, eliminating the precedence ambiguity
entirely for any expression kind (literals, helper calls, functional pipelines, etc.).

### Minimal reproducer

```moss
fn total(values: Vector[Int]) -> Int:
  var result = 0
  for index in range(0, values |> count):
    result = result + values[index]
  return result

fn main():
  echo total([3, 4])
```

Committed reproducer: `examples/swarm/control_flow_2026/simulation_engine/failed_attempts/01_for_native_reproducer/`.

### Observed behavior (before fix)

`moss fmt` accepts the source. `moss check` passes. Fast Debug reports its documented
limitation (`interpreter error: Fast Debug does not support for iteration yet`).

Native compilation failed with:

```
error[E0425]: cannot find value `index` in this scope
error[E0308]: mismatched types — expected `()`, found `i64`
```

### Classification distinction from SWARM-057

SWARM-057 describes `for i in range(...)` causing `TYPE_INFERENCE_FAILED` specifically inside an
explicit module scope (a module/package-boundary defect). SWARM-066 is a different failure:
the same construct is accepted by the checker in a plain single-file program but the native
lowering backend emits invalid Rust. Distinct compiler phases, distinct failure signatures.

### Notes

Non-boundary defect: fails in a single-file program without any module structure. Formatter and
checker fully accept the source, making this a native-lowering-only parity gap. Fixed in
Phase 15.14 by materializing range bounds before the for-loop header.
