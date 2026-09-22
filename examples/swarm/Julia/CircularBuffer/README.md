# Julia `CircularBuffer` → Moss

This dogfooding experiment translates Julia's `CircularBuffer` from
[JuliaCollections/DataStructures.jl](https://github.com/JuliaCollections/DataStructures.jl)
into a pure Moss `Int` circular buffer.

## Source

The input is modeled after `CircularBuffer` in `JuliaCollections/DataStructures.jl`:
- Fixed capacity buffer.
- `push`: appends an item to the back; when full, overwrites the oldest element at the front.
- `pop`: removes and returns the newest item from the back (matching Julia's `pop!`).
- `popfirst`: removes and returns the oldest item from the front (matching Julia's `popfirst!`).
- `first` / `last`: inspect front and back elements.
- `capacity`, `length`, `isempty`, `isfull`: query buffer state.
- `get(index)`: 0-based logical index access from oldest to newest.

## Translation

- Moss vectors are zero-based. Logical indices are mapped to circular array positions:
  - `idx = start + offset`
  - `if idx >= cap: idx = idx - cap`
- When full (`length == capacity`), `push` overwrites the item at `start` and increments `start` (wrapping around at `cap`).
- `pop` decrements `length` and retrieves `data[(start + len - 1) % cap]`.
- `popfirst` increments `start` and decrements `length`.
- Trait specialization: `trait BoundedBuffer` demonstrates compile-time structural trait specialization.
- Static duck typing: `inspect_capacity(target)` demonstrates untyped static dispatch.

## Run

From this directory:

```sh
../../../../margo test
../../../../margo build
../../../../margo run
```
