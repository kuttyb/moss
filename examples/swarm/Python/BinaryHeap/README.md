# Python `heapq` → Moss

This independent dogfooding experiment translates the bounded `Int` min-heap
core of CPython's `heapq` into pure Moss. It implements empty construction,
`push`, `peek`, and `pop` (the `heappop` equivalent), but not heapify,
replacement operations, max heaps, or the other `heapq` APIs.

## Source

The input was CPython
[`212e6035133957a66f1a823e012b4dc2a5158ce8`](https://github.com/python/cpython/commit/212e6035133957a66f1a823e012b4dc2a5158ce8),
specifically [`Lib/heapq.py`](https://github.com/python/cpython/blob/212e6035133957a66f1a823e012b4dc2a5158ce8/Lib/heapq.py).
The translation uses `heappush`, `heappop`, `_siftdown`, and `_siftup` only.

## Translation

Python and Moss both use zero-based indexing, so the source relationships carry
over directly: the left child is `2 * position + 1`, the right child is one
greater. CPython computes the parent as `(pos - 1) >> 1`; Moss uses
`(position - 1) / 2`. For the nonnegative indices reachable in this heap
algorithm, those calculations are equivalent. The project test verifies the
Moss integer-division behavior relied on by this translation.

`push` appends and calls `sift_down(0, count() - 1)`. `pop` removes the
last vector element, installs it at the root when needed, and calls `sift_up`.
The Moss `sift_up` deliberately follows CPython's two-stage shape: it bubbles
the smaller child path to a leaf, places the saved item there, then calls
`sift_down` to move that item toward its final position. It is not rewritten as
a textbook or Julia-style direct percolate-down loop.

The selected representation is a `BinaryHeap` with `Vector[Int]` storage. The initial `[]` receives its element type from the
`data: Vector[Int]` field; no sentinel element is used.

## What worked naturally

- Ordinary `type` methods, local values, `while`, and `if` express the heap
  state transitions.
- `Int` arithmetic and zero-based vector indexing map directly from CPython.
- Indexed assignment, `Vector.push`, and `Vector.pop` support the requested
  mutation pattern.
- The project tests exercise ordering, duplicates, negative values, a
  single-element heap, and reuse after pops.

## Historical friction (resolved)

The original independent port used an explicit `size`, scalar indexed-read
temporaries, a statement-plus-return helper form, `take_min`, and `0 - N`
assertions. These came from `(data |> count) - 1`,
`data[right] <= data[child]`, `return sift_down(start, current)`, `fn pop()`,
and `assertEqual(heap.take_min(), -4)`. They were fixed later by `50ff4b7`,
`85b00d6`, `6718255`, `b0d17e4`, and `814df22`, respectively. The checked-in
source now uses the natural forms; the original reproducers and workarounds
remain in the [findings ledger](../../FINDINGS.md).

These are bounded experiment observations, not language-design recommendations.

## Comparison with Julia experiment

Both the independent Python and frozen Julia ports succeeded and independently
selected a `Vector[Int]` representation. Both independently encountered
arithmetic `count` inference, native lowering of a user method named `pop`, and
the unary-negative `assertEqual` formatter issue; those original findings are
now fixed.

Python's zero-based source avoided Julia's one-based index conversion, but did
not remove the need to verify parent arithmetic. The source algorithms produce
meaningfully different Moss code: Julia's percolate-down places a saved value
while moving smaller children only until it fits, whereas CPython's `sift_up`
first moves the smaller-child path all the way to a leaf and then invokes
`sift_down`. The Python experiment additionally recorded direct indexed-child
comparison and direct helper-return inference friction. The Julia report does
not list those as separate findings.

## Run

From this directory:

```sh
../../../../margo test
../../../../margo build
../../../../margo run
```

The program prints `1 1 2 5 7`: the first value is `peek`, followed by four
`pop` results.
