# Python `heapq` → Moss

This independent dogfooding experiment translates the bounded `Int` min-heap
core of CPython's `heapq` into pure Moss. It implements empty construction,
`push`, `peek`, and `take_min` (the `heappop` equivalent), but not heapify,
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

`push` appends and calls `sift_down(0, size - 1)`. `take_min` removes the
last vector element, installs it at the root when needed, and calls `sift_up`.
The Moss `sift_up` deliberately follows CPython's two-stage shape: it bubbles
the smaller child path to a leaf, places the saved item there, then calls
`sift_down` to move that item toward its final position. It is not rewritten as
a textbook or Julia-style direct percolate-down loop.

The selected representation is a `BinaryHeap` with `Vector[Int]` storage and
an explicit `size: Int`. The initial `[]` receives its element type from the
`data: Vector[Int]` field; no sentinel element is used.

## What worked naturally

- Ordinary `type` methods, local values, `while`, and `if` express the heap
  state transitions.
- `Int` arithmetic and zero-based vector indexing map directly from CPython.
- Indexed assignment, `Vector.push`, and `Vector.pop` support the requested
  mutation pattern.
- The project tests exercise ordering, duplicates, negative values, a
  single-element heap, and reuse after pops.

## Friction and workarounds

**Moss compiler/implementation limitation — arithmetic vector length.** Python
uses `len(heap) - 1` for `heappush`. The natural Moss attempt
`(data |> count) - 1` reports `TYPE_INFERENCE_FAILED: cannot infer the type of
this return expression`. The compiler's type query reports the same failure.
The heap therefore updates `size: Int` next to each vector push/pop.

**Moss compiler/implementation limitation — two indexed child reads.** CPython
uses `not heap[childpos] < heap[rightpos]` while selecting the smaller child in
`_siftup`. The direct Moss comparison `data[right] <= data[child]` reports
`MOSS_COMPILE_ERROR: value is not an indexable container`; `moss type` reports
the same location. Reading both values into scalar locals before comparing them
preserves the source algorithm.

**Moss compiler/implementation limitation — direct helper return.** The direct
translation `return sift_down(start, current)` at the end of `_siftup` reports
`TYPE_INFERENCE_FAILED`. Calling `sift_down(start, current)` as a statement and
returning the already-known local `current` is accepted; the heap mutation is
unchanged.

**Moss compiler/implementation limitation — public `pop` name.** A natural
`fn pop() -> Int` checks, but native build lowers a call as nonexistent
`BinaryHeap.pop_front`. The pop-equivalent is named `take_min` instead.

**Tooling/diagnostic problem — unary negative expectations.** The checker
accepts `assertEqual(heap.take_min(), -4)`, but `moss fmt` reports that it
cannot infer the assertion operand types. Expected negatives use `0 - 4` and
`0 - 1`; heap inputs remain ordinary negative literals.

These are bounded experiment observations, not language-design recommendations.

## Comparison with Julia experiment

Both the independent Python and frozen Julia ports succeeded and independently
selected a `Vector[Int]` plus explicit `size: Int` representation. Both
independently encountered arithmetic `count` inference, native lowering of a
user method named `pop`, and the unary-negative `assertEqual` formatter issue.

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
`take_min` results.
