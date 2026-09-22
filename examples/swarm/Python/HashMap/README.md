# Moss `IntHashMap` (Open Addressing with Linear Probing)

This dogfooding experiment implements a pure Moss `Int -> Int` hash map (`IntHashMap`) using open addressing with linear probing and a structure-of-arrays (SoA) layout.

## Design and Architecture

### 1. Structure-of-Arrays Storage
Rather than an array of boxed entry objects, `IntHashMap` uses three parallel primitive vectors:
- `keys: Vector[Int]`: Active and tombstone keys.
- `values: Vector[Int]`: Associated values.
- `states: Vector[Int]`: Slot states:
  - `0`: **Empty** (slot never occupied; stops search).
  - `1`: **Occupied** (active key-value pair).
  - `2`: **Tombstone** (deleted slot; searches probe past it, insertions reuse it).

This layout avoids nested containers, dynamic dispatch, or complex lvalue reference chains while keeping storage flat, contiguous, and cache-friendly.

### 2. Multiplicative Integer Hashing & Modulo
Moss v0.1 supports integer arithmetic (`+`, `-`, `*`, `/`) but does not include `%` as an arithmetic operator. Modulo is implemented via truncating division and subtraction:
```moss
mixed = k * 31 + 17
quotient = mixed / cap
var idx = mixed - quotient * cap
if idx < 0:
  idx = idx + cap
```
This guarantees non-negative, well-distributed initial slot indices for positive, negative, and zero keys.

### 3. Linear Probing with Tombstone Reuse
- **`get(key, default_val)` & `contains(key)`**: Probe sequentially from `hash_index(key)` until finding an occupied slot matching `key` or encountering an empty slot (`state == 0`). Tombstones (`state == 2`) are skipped.
- **`put(key, value)`**: Probes past tombstones to check if the key already exists (updating in place if found). If absent, inserts into the first encountered tombstone slot or the first empty slot, setting `state = 1` and incrementing `count`.
- **`remove(key)`**: Sets the slot's state to `2` (tombstone) and decrements `count`.

### 4. Dynamic Resizing
When the load factor reaches 70% (`count * 10 >= cap * 7`), `resize(cap * 2)` is triggered. Elements are saved and rehashed into new slots in place, ensuring `self` is never consumed and memory is reused cleanly.

### 5. Structural Trait Specialization
The map conforms to a compile-time structural trait `MapLike`:
```moss
trait MapLike:
  fn size() -> Int
  fn is_empty() -> Bool
  fn contains(key: Int) -> Bool
  fn get(key: Int, default_val: Int) -> Int
```
Functions like `map_size(m: MapLike)` and `map_has(m: MapLike, key: Int)` statically duck-type specialize to `IntHashMap` without nominal declarations.

## What Worked Naturally

- **Primitive Vector Operations**: `keys[idx] = key`, `push(0)`, and `pop()` expressed the storage operations directly.
- **Structural Traits**: Static specialization through `MapLike` compiled without trait objects or vtables.
- **Effect Inference**: The compiler correctly inferred `WRITE` effects for methods mutating map state and `READ` effects for queries.
- **Unit Tests & Assertions**: `test "..."` blocks with `assertEqual` and `assert` verified all operations.

## Key Learnings & Compiler Boundary Observations

1. **No `%` Operator**: Moss v0.1 does not treat `%` as a binary arithmetic operator. Modulo was cleanly implemented via `x - (x / cap) * cap`.
2. **Local Variable Member Calls**: Calling `.push()` on a local variable requires the local variable to have an inferred collection type (e.g. `var v = [0]; v.pop()`). Writing `var v: Vector[Int] = []` was parsed with the type attached to the identifier name.
3. **In-Place Mutation vs Field Rebinding**: Rebinding a field with an owned container (`keys = next_keys`) inferred a `CONSUME` effect on `self`. Mutating the vector in place (`keys.pop()`, `keys.push()`, `keys[idx] = ...`) inferred a `WRITE` effect, allowing subsequent calls without ownership conflicts.
4. **Rust Prelude Name Collision**: Naming the type `HashMap` collided with Rust's `std::collections::HashMap` during native lowering (`error[E0116]: cannot define inherent impl for a type outside of the crate`). Naming it `IntHashMap` resolved the collision.

## Validation

All tests pass natively via `margo test`:
```text
PASS src/main.moss:clears all entries
PASS src/main.moss:dynamically resizes when exceeding load factor threshold
PASS src/main.moss:empty hash map reports correct initial state
PASS src/main.moss:handles negative keys and zero
PASS src/main.moss:inserts and retrieves single and multiple key-value pairs
PASS src/main.moss:overwrites value for existing key without increasing size
PASS src/main.moss:removes keys and allows lookup past tombstones
PASS src/main.moss:reuses tombstone slots on subsequent insert
PASS src/main.moss:snapshot vectors for keys and values
PASS src/main.moss:supports structural MapLike trait

10 passed
0 failed
```
