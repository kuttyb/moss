# Python `ChainMap` → Moss Implementation

## Result

Complete success. The checked Moss source implements the meaningful core of Python's `collections.ChainMap` for `String -> Int` mappings, supporting multi-map composition, in-order prioritized lookup, containment search, defaulted lookup, front-only mutation, child/front scope derivation (`new_child`, `new_child_empty`), parent/rest view derivation (`parents`), and key/value flattening (`flatten`, `keys`, `values`).

All 13 unit tests pass natively under `margo test` and execute cleanly under Fast Debug (`moss run --interp` and `margo debug`).

## Source Reference

- Repository reference: [`examples/swarm/Python/chain_map/reference/chain_map.py`](reference/chain_map.py)
- Upstream origin: Python standard library `collections.ChainMap` (`Lib/collections/__init__.py`)
- Core semantics preserved:
  1. Multiple mappings stored in a prioritized lookup sequence.
  2. Front-to-back prioritized lookup (`get` and key searching).
  3. Safe fallback/default lookup when keys are missing.
  4. Containment checking (`contains`).
  5. Front-only mutation (`set` / `__setitem__`): mutations write strictly to `maps[0]`, never touching subsequent parent maps.
  6. Front-map shadowing: writing a key to the front map shadows any definition in parent maps without mutating them.
  7. Scope extension (`new_child` / `new_child_empty`): creates a new child ChainMap prepending a child map in front of the existing chain.
  8. Scope retraction (`parents`): creates a new ChainMap viewing all maps except the first.
  9. Key/value flattening (`flatten`, `keys`, `values`): consolidates active bindings in chain order.

## Value-Snapshot Semantics vs Shared-View Aliasing

A notable semantic distinction exists between Python's reference-aliased collections and Moss's value model:
- **Python `ChainMap`**: Stores a list of references to mutable dictionaries (`self.maps = list(maps)`). Mutations made directly to an underlying dictionary or via another alias are immediately visible across all `ChainMap` instances sharing that dictionary reference.
- **Moss `ChainMap`**: Moss enforces value semantics with deterministic ownership. In accordance with Moss design principles, `ChainMap` operates on value snapshots:
  - Child creation (`new_child`) snapshots existing mappings into independent mutable storage for the child chain.
  - Front-only mutation (`set`) clones and updates the front mapping without mutating parent scopes.
  - Scope retraction (`parents`) creates an independent snapshot of subsequent mappings.
  - This guarantees scope isolation: child scopes cannot corrupt parent mappings, and parents cannot introduce race conditions or mutation side-effects into active child scopes.

## Design and Architecture

### 1. Vector of Maps Representation
`ChainMap` is defined as a composite structure holding a vector of concrete maps:
```moss
type ChainMap:
  maps: Vector[Map[String, Int]]
```
This stores arbitrary numbers of mappings in sequential order, matching Python's `self.maps = list(maps)`.

### 2. Concrete Map Typing & Cloning
Because Moss v0.1 does not provide runtime generic type parameters or dynamic `Map[K, V]` clone operators, a small helper record `MapHolder` contextually types newly allocated empty maps:
```moss
type MapHolder:
  m: Map[String, Int]

fn empty_map() -> Map[String, Int]:
  var holder = MapHolder(m: Map())
  return holder.m
```
Deep copying maps (`clone_map`) is accomplished through explicit iteration over keys, ensuring independent mutable storage when constructing children or mutating the front mapping.

### 3. Read-Only Collection Access
Moss infers parameter effects as `READ`, `WRITE`, or `CONSUME`. Collections indexed within methods (`maps[i]`) can be queried directly via method calls like `maps[i].get(key, default)` and `maps[i].keys()`.

### 4. Front-Only Mutation
Python's `ChainMap` specifies that all mutations (insertions and updates) apply only to the first mapping (`self.maps[0]`). In Moss, `set` updates the front map while keeping all other maps strictly read-only:
```moss
fn set(key: String, val: Int):
  var m0 = clone_map(maps[0])
  m0[key] = val
  maps[0] = m0
```
This preserves the invariant that parent scopes cannot be corrupted by child mutations.

### 5. Scope Management
- **`new_child(child_map)`**: Prepend `child_map` in front of current mappings and return a new `ChainMap`.
- **`new_child_empty()`**: Creates a child ChainMap with an empty front map via self-method delegation `self.new_child(empty_map())`.
- **`parents()`**: Extracts maps `1..N` into a new `ChainMap`. If only one map was present, defaults to a single empty map (matching Python's `ChainMap(*self.maps[1:])` fallback).

## Friction Encountered and Compiler Resolutions

During initial fresh-agent implementation, several boundary behaviors were identified and classified. In the Phase 15.10 corrective closeout pass, these were resolved as follows:

1. **`key in map` Expression (Finding A / SWARM-026)**:
   - *Issue*: Binary `in` syntax passed frontend type checking but failed in interpreter (`unsupported expression '"a" in m'`) and rustc syntax parsing.
   - *Resolution*: Binary `in` is not part of the Moss v0.1 expression grammar. The checker now cleanly rejects binary `in` with stable diagnostic `UNSUPPORTED_EXPRESSION_OPERATOR` and instructs developers to use `Map.get`, key iteration, or explicit search.
   - *Code pattern*: Use `map_contains` or `m.get(key, default)` instead of `key in m`.

2. **Method Dispatch on Indexed Collection Receivers (Finding B / SWARM-027)**:
   - *Issue*: Calling methods on indexed elements like `cm.maps[0].get("b", 0)` or `maps[i].keys()` failed native lowering because `generated_expr_type` did not preserve the inner element type of indexed expressions, defaulting to Rust's native 1-argument `HashMap::get` and `.keys()` iterator.
   - *Resolution*: Extended `generated_expr_type` to resolve index expressions (`parse_index`), preserving inner collection types (`Vector[T] -> T`, `Map[K, V] -> V`). Method calls on indexed elements now dispatch cleanly to Moss collection methods.
   - *Code pattern*: Direct `cm.maps[0].get("b", 0)` and `maps[i].keys()` calls work without intermediate helper functions.

3. **Self-Method Inter-Calling (Finding C / SWARM-028)**:
   - *Issue*: Invoking sibling methods on `self` (`self.new_child(empty_map())` or `helper(m)`) failed in native lowering because the object code generator omitted `types["self"] = t.name` and expression-level simple calls did not resolve sibling method parameter effects, causing arguments to be emitted as owned values rather than borrowed references.
   - *Resolution*: Populated `types["self"]` in `gen_object` and `check_objects`, and added sibling method resolution to expression call lowering so inferred parameter effects (`READ` / `WRITE`) correctly borrow arguments.
   - *Code pattern*: Methods can freely invoke other methods on `self` (`new_child_empty()` calls `self.new_child(empty_map())`).

4. **Nested Indexed Mutation (Finding D / SWARM-029)**:
   - *Issue*: Assigning to nested index targets like `maps[0][key] = value` was attempted by analogy to Python.
   - *Resolution*: Confirmed that nested indexed assignment is outside the Moss v0.1 specification. The checker now rejects nested index assignment with stable diagnostic `UNSUPPORTED_NESTED_INDEX_ASSIGNMENT`, directing users to extract the inner collection to a local variable, update it, and write it back.

5. **String Comparison Between Owned and Borrowed Operands (Finding E / SWARM-030)**:
   - *Issue*: Comparing `k == key` inside `map_contains` (where `k` is an owned `String` from `keys[i]` and `key` is a borrowed `&String` parameter) lowered to `(k) == (key)`, which failed rustc type checking because Rust does not implement `PartialEq` across `String` and `&String`.
   - *Resolution*: Comparison lowering now wraps string operands with `.as_str()`, enabling uniform `&str == &str` comparison across owned and borrowed representations. Additionally, `ConditionState` in handler lowering now populates borrowed message parameters so comparisons in branch conditions lower correctly.
   - *Code pattern*: Direct `k == key` comparisons compile and execute cleanly.

## Validation

All tools and execution modes were verified:

1. **Canonical Formatting**:
   ```sh
   ./moss fmt examples/swarm/Python/chain_map/src/main.moss
   ```
   Formatted cleanly and idempotently.

2. **Semantic Check**:
   ```sh
   ./moss check examples/swarm/Python/chain_map/src/main.moss --json
   ```
   Passes with 0 diagnostics (`"ok": true`).

3. **Native Root-Package Tests**:
   ```sh
   ./margo test
   ```
   All 13 tests pass:
   - `PASS src/main.moss:assignment modifies only the front map`
   - `PASS src/main.moss:child front scope sees parent mappings`
   - `PASS src/main.moss:empty chain map defaults`
   - `PASS src/main.moss:fallback finds a key in a later map`
   - `PASS src/main.moss:flatten produces single combined map with front priority`
   - `PASS src/main.moss:front map wins lookup`
   - `PASS src/main.moss:keys and values extraction`
   - `PASS src/main.moss:missing lookup follows selected representation`
   - `PASS src/main.moss:multi level child scope hierarchy`
   - `PASS src/main.moss:multiple maps preserve lookup order`
   - `PASS src/main.moss:new child empty`
   - `PASS src/main.moss:parent rest operation works`
   - `PASS src/main.moss:shadowing works`

4. **Native Execution**:
   ```sh
   ./margo run
   ```
   Outputs expected configuration overlay values:
   ```text
   theme: 2
   show_line_numbers: 1
   ```

5. **Fast Debug Execution**:
   ```sh
   ./moss run --interp examples/swarm/Python/chain_map/src/main.moss
   ./margo debug
   ./margo debug --trace
   ```
   Executes cleanly in interpreter mode and produces structured NDJSON execution traces matching native output.
