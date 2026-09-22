# Moss `HashMap` (Open Addressing with Linear Probing)

This dogfooding experiment implements a pure Moss `Int -> Int` hash map (`HashMap`) using open addressing with linear probing and a structure-of-arrays (SoA) layout. It is a Moss collection experiment placed under the Python swarm area, not a line-by-line translation of a pinned CPython `dict` implementation.

## Design and Architecture

### 1. Structure-of-Arrays Storage
Rather than an array of boxed entry objects, `HashMap` uses three parallel primitive vectors:
- `keys: Vector[Int]`: Active and tombstone keys.
- `values: Vector[Int]`: Associated values.
- `states: Vector[Int]`: Slot states:
  - `0`: **Empty** (slot never occupied; stops search).
  - `1`: **Occupied** (active key-value pair).
  - `2`: **Tombstone** (deleted slot; searches probe past it, insertions reuse it).

This layout avoids nested containers, dynamic dispatch, or complex lvalue reference chains while keeping storage flat, contiguous, and cache-friendly.

### 2. Multiplicative Integer Hashing & Modulo
The original experiment used truncating division and subtraction because `%` was unavailable. Current Moss uses integer remainder directly:
```moss
mixed = k * 31 + 17
var idx = mixed % cap
if idx < 0:
  idx = idx + cap
```
For nonzero capacity, `%` follows Moss's truncating integer-division semantics. The historical workaround is preserved in `SWARM-014`.

### 3. Linear Probing with Tombstone Reuse
- **`get(key, default_val)` & `contains(key)`**: Probe sequentially from `hash_index(key)` until finding an occupied slot matching `key` or encountering an empty slot (`state == 0`). Tombstones (`state == 2`) are skipped.
- **`put(key, value)`**: Probes past tombstones to check if the key already exists (updating in place if found). If absent, inserts into the first encountered tombstone slot or the first empty slot, setting `state = 1` and incrementing `count`.
- **`remove(key)`**: Sets the slot's state to `2` (tombstone) and decrements `count`.

### 4. Dynamic Resizing
When the load factor reaches 70% (`count * 10 >= cap * 7`), `resize(cap * 2)` is triggered. Elements are saved and rehashed into new slots in place. The original report that direct owned-field replacement consumes `self` was reduced and did not reproduce: field replacement consumes the RHS while leaving the receiver available.

### 5. Structural Trait Specialization
The map conforms to a compile-time structural trait `MapLike`:
```moss
trait MapLike:
  fn size() -> Int
  fn is_empty() -> Bool
  fn contains(key: Int) -> Bool
  fn get(key: Int, default_val: Int) -> Int
```
Functions like `map_size(m: MapLike)` and `map_has(m: MapLike, key: Int)` statically duck-type specialize to `HashMap` without nominal declarations.

## What Worked Naturally

- **Primitive Vector Operations**: `keys[idx] = key`, `push(0)`, and `pop()` expressed the storage operations directly.
- **Typed Empty Vectors**: `Vector[Int]()` now constructs empty rehash and snapshot storage without a dummy value.
- **Structural Traits**: Static specialization through `MapLike` compiled without trait objects or vtables.
- **Effect Inference**: The compiler correctly inferred `WRITE` effects for methods mutating map state and `READ` effects for queries.
- **Unit Tests & Assertions**: `test "..."` blocks with `assertEqual` and `assert` verified all operations.

## Key Learnings & Compiler Boundary Observations

1. **Integer remainder**: The original `x - (x / cap) * cap` workaround was fixed later as `SWARM-014`; current source uses `%`.
2. **Local annotation spelling**: `var v: Vector[Int] = []` remains unsupported and now receives `LOCAL_TYPE_ANNOTATION_UNSUPPORTED`. Current source uses the purpose-built `Vector[Int]()` constructor (`SWARM-017`).
3. **Field replacement**: The initial `keys = next_keys` concern was not reproduced by the minimal ownership probe. The receiver remains available and the RHS is consumed, so this is an agent misunderstanding rather than a compiler finding.
4. **Rust backend names**: The original `HashMap` name collided with a backend import during native lowering. `SWARM-015` fully qualifies runtime collection types, so the checked-in type is again named `HashMap`.

## Validation

All tests pass natively via `margo test`:
```text
PASS src/main.moss:clears all entries
PASS src/main.moss:dynamically resizes when exceeding load factor threshold
PASS src/main.moss:empty hash map reports correct initial state
PASS src/main.moss:handles negative keys and zero
PASS src/main.moss:inserts and retrieves single and multiple key-value pairs
PASS src/main.moss:looks up and updates beyond a tombstone
PASS src/main.moss:normalizes negative keys in a collision chain
PASS src/main.moss:overwrites value for existing key without increasing size
PASS src/main.moss:probes a collision chain
PASS src/main.moss:removes keys and allows lookup past tombstones
PASS src/main.moss:reuses tombstone slots on subsequent insert
PASS src/main.moss:snapshot vectors for keys and values
PASS src/main.moss:supports structural MapLike trait

13 passed
0 failed
```
