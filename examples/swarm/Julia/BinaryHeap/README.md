# Julia `BinaryHeap` → Moss

This small dogfooding experiment translates the core algorithm of
JuliaCollections/DataStructures.jl's `BinaryHeap` into a pure Moss `Int`
min-heap. It deliberately implements only empty construction, `push`, `peek`,
`take_min` (the equivalent of `pop`), and the small `count`/`empty` helpers
required by the example.

## Source

The input was the `master` revision
[`0ed46fbdb4bbfded67bd463814a3899b0c08444c`](https://github.com/JuliaCollections/DataStructures.jl/commit/0ed46fbdb4bbfded67bd463814a3899b0c08444c)
of [JuliaCollections/DataStructures.jl](https://github.com/JuliaCollections/DataStructures.jl):

- [`src/heaps/binary_heap.jl`](https://github.com/JuliaCollections/DataStructures.jl/blob/0ed46fbdb4bbfded67bd463814a3899b0c08444c/src/heaps/binary_heap.jl)
- [`src/heaps/arrays_as_heaps.jl`](https://github.com/JuliaCollections/DataStructures.jl/blob/0ed46fbdb4bbfded67bd463814a3899b0c08444c/src/heaps/arrays_as_heaps.jl)

The translation uses the array-level `percolate_up!`, `percolate_down!`,
`heappush!`, and `heappop!` logic, rather than Julia's generic ordering and
constructor surface.

## Translation

Julia's heap array is one-based; Moss vectors are zero-based. The index mapping
therefore changes the relationships to:

```text
left(i)   = 2 * i + 1
right(i)  = 2 * i + 2
parent(i) = (i - 1) / 2
```

The implementation retains Julia's hole-moving shape: it saves the inserted or
replacement value, moves parents or smaller children into the hole, and stores
the saved value once. The last vector element is read before `Vector.pop()` removes it.
As in the bounded source experiment, calling `take_min` or `peek` on an empty heap
is outside the exercised contract.

## What worked naturally

- `type` fields and ordinary methods model the heap directly.
- `while`, `if`, `Int` arithmetic, comparisons, zero-based indexing, indexed
  vector assignment, `Vector.push`, and `Vector.pop` express the core algorithm.
- An empty `[]` obtains its `Int` element type from the `BinaryHeap.data` field
  at construction.
- Integer division is checked by the project test: `5 / 2 == 2`, which supplies
  the zero-based parent calculation used here.
- The tests cover the requested ordering sequence, duplicates, negatives, a
  single element, and reuse after pops.

## Friction and workaround

**Moss implementation/compiler limitation — vector length in arithmetic.** The
natural current spelling, `data |> count`, checks as a terminal or comparison,
but the probe `return (data |> count) + 1` is rejected with
`TYPE_INFERENCE_FAILED: cannot infer the type of this return expression`.
The heap needs arithmetic lengths to derive the last index and child bounds.

The bounded workaround is the explicit `size: Int` field, updated beside each
successful vector `push`/`pop`. This is not a sentinel element, does not hide the
algorithm behind another collection, and keeps all heap ordering/mutation in
Moss. It is an experiment observation, not a language-change proposal.

**Moss implementation/compiler limitation — user method named `pop`.** A direct
`fn pop() -> Int` form checks, but native lowering resolves calls as a collection
`pop_front` operation on `BinaryHeap`, which does not exist. The experiment calls
the equivalent operation `take_min` instead. This is a naming workaround only;
the method still performs the source algorithm's `heappop!` behavior.

**Tooling/diagnostic problem — unary negative in `assertEqual`.** The checker
accepts `assertEqual(heap.take_min(), -4)`, but the formatter reports that it
cannot infer the assertion operand types. The two negative expected values are
therefore written as `0 - 4`, while the heap still receives ordinary `-4` inputs.

## Run

From this directory, use the repository-local project driver:

```sh
../../../../margo test
../../../../margo build
../../../../margo run
```

The program prints the pop order `1 2 5 7`.
