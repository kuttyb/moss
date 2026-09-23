# Python `ChainMap` → Moss Implementation

## Result

Complete success. The checked Moss source implements the meaningful core of Python's `collections.ChainMap` for `String -> Int` mappings, supporting multi-map composition, in-order prioritized lookup, containment search, defaulted lookup, front-only mutation, child/front scope derivation (`new_child`), parent/rest view derivation (`parents`), and key/value flattening (`flatten`, `keys`, `values`).

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
  7. Scope extension (`new_child`): creates a new child ChainMap prepending a child map in front of the existing chain.
  8. Scope retraction (`parents`): creates a new ChainMap viewing all maps except the first.
  9. Key/value flattening (`flatten`, `keys`, `values`): consolidates active bindings in chain order.

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

### 3. Read-Only Borrow Invariance
Moss infers parameter effects as `READ`, `WRITE`, or `CONSUME`. Direct index operations on collections (e.g. `v[i]`) can trigger value transfers if bound to local variables. By encapsulating map inspection in read-only helper functions (`map_contains`, `map_item`, `map_keys`), map reads borrow from `maps[i]` without consuming the containing vector.

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
- **`new_child(child_map)`**: Prepend `child_map` in front of the current maps and return a new `ChainMap`.
- **`chain_map_new_child_empty(cm)`**: Creates a child ChainMap with an empty front map.
- **`parents()`**: Extracts maps `1..N` into a new `ChainMap`. If only one map was present, defaults to a single empty map (matching Python's `ChainMap(*self.maps[1:])` fallback).

## Friction Encountered and Compiler Boundaries

1. **`key in map` Expression Backend Gap**:
   - The Moss frontend parses and checks `if "a" in m:`, but Fast Debug interpreter fails with `unsupported expression '"a" in m'`, and native lowering emits raw Rust `if "a" in m {` which fails rustc syntax parsing.
   - *Resolution*: Implemented `map_contains(m, key)` which inspects `map.keys()`.

2. **Rust Backend `String == &String` Comparison Mismatch**:
   - Inside `map_contains`, comparing a `String` from `keys[i]` directly against a `key: String` function parameter lowered to Rust as `(keys[i]) == (key)`, which rustc rejected because `key` was a borrowed parameter `&String` while `keys[i]` was an owned `String`.
   - *Resolution*: Materialized an owned string `target = "" + key` prior to the loop.

3. **Method-on-Vector-Element Lowering**:
   - Calling `cm.maps[0].get("b", 0)` in test assertions lowered to `(cm.maps)[0].get("b".to_string(), 0_i64)` in Rust, which called Rust's standard `HashMap::get` (1 argument) rather than Moss's 2-argument `Map.get(key, default)`.
   - Calling `cm.maps[i].keys()` in a method lowered to Rust's `.keys()` iterator instead of Moss's `Vector` snapshot.
   - *Resolution*: Channeled map operations on indexed vector elements through typed helper functions (`map_get`, `map_keys`, `map_item`).

4. **Self-Method Inter-Calling Lowering**:
   - Calling `self.new_child(empty_map())` from another method inside `ChainMap` failed in native lowering because `empty_map()` was passed as an owned value rather than borrowed as expected by the lowered method signature `&HashMap`.
   - *Resolution*: Exposed top-level helpers (`chain_map_new_child_empty`, `chain_map_values`) for composable cross-method utilities.

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
   Outputs expected configuration overlay values.

5. **Fast Debug Execution**:
   ```sh
   ./moss run --interp examples/swarm/Python/chain_map/src/main.moss
   ./margo debug
   ./margo debug --trace
   ```
   Executes cleanly in interpreter mode and produces structured NDJSON execution traces.
