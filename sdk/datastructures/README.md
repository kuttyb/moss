# moss.datastructures

`moss.datastructures` provides concrete priority-oriented structures for Moss
v0.1: integer min/max heaps, a stable integer priority queue, and a fixed-size
integer ring deque. The package has no dependencies and uses no domains,
Executors, dynamic comparators, or recursion.

Add it to a consumer manifest with package name `moss.datastructures`, then use
`import datastructures`.

## Ownership and validation

Constructors own their storage. `min_heap_from` and `max_heap_from` copy the
input vector before applying bottom-up heapify, so later heap mutation does not
change the caller's vector. `int_deque_values` returns an owned logical-order
copy.

Moss v0.1 exported records are directly constructible. Every public operation
therefore validates its record before indexing and raises
`DataStructureError.InvalidState` for malformed heap order, mismatched priority
queue vectors, invalid or duplicate sequence IDs, invalid queue order, or bad
deque metadata. The explicit `*_validate` functions return `false` instead.

This safety rule has an important complexity consequence: full public-record
validation is O(n). The heapify constructor is O(n), and the underlying
sift-up/sift-down mutation is O(log n), but a public heap or priority-queue
operation is O(n) including mandatory validation. Deque validation is O(1), so
both-end deque operations remain O(1). A future opaque-record facility could
preserve forged-record rejection while making validated heap operations
end-to-end O(log n); this package does not hide the current cost.

## Errors

`DataStructureError` is a closed typed enum. This workstream uses:

- `Empty` for peek/pop on an empty structure.
- `Full(capacity)` for insertion into a full deque.
- `InvalidCapacity(capacity)` for deque capacity less than or equal to zero.
- `InvalidIndex(index, length)` for deque random access outside `0..length`.
- `SequenceOverflow` when the stable priority sequence is exhausted.
- `CapacityOverflow` before a vector count could overflow Moss `Int`.
- `InvalidState` for malformed directly constructed records.

`InvalidSize` and `NotFound` are reserved by the shared package error contract
for later non-priority structures.

Failed mutators validate and reject before changing storage. Equal priority
values dequeue in insertion order. Lower integer priority values dequeue first.

## Public API

### Min and max heaps

```moss
new_min_heap() -> IntMinHeap
min_heap_from(values: Vector[Int]) -> IntMinHeap
min_heap_len(heap: IntMinHeap) -> Int
min_heap_is_empty(heap: IntMinHeap) -> Bool
min_heap_validate(heap: IntMinHeap) -> Bool
min_heap_peek(heap: IntMinHeap) -> Int
min_heap_push(heap: IntMinHeap, value: Int)
min_heap_pop(heap: IntMinHeap) -> Int

new_max_heap() -> IntMaxHeap
max_heap_from(values: Vector[Int]) -> IntMaxHeap
max_heap_len(heap: IntMaxHeap) -> Int
max_heap_is_empty(heap: IntMaxHeap) -> Bool
max_heap_validate(heap: IntMaxHeap) -> Bool
max_heap_peek(heap: IntMaxHeap) -> Int
max_heap_push(heap: IntMaxHeap, value: Int)
max_heap_pop(heap: IntMaxHeap) -> Int
```

### Stable min-priority queue

```moss
new_priority_queue() -> StableIntPriorityQueue
priority_queue_len(queue: StableIntPriorityQueue) -> Int
priority_queue_is_empty(queue: StableIntPriorityQueue) -> Bool
priority_queue_validate(queue: StableIntPriorityQueue) -> Bool
priority_queue_push(queue: StableIntPriorityQueue, priority: Int, value: Int)
priority_queue_peek(queue: StableIntPriorityQueue) -> PriorityItem
priority_queue_pop(queue: StableIntPriorityQueue) -> PriorityItem
```

`PriorityItem` exposes `priority` and `value`. The internal sequence remains an
implementation detail, while the exported record retains it for validation.

### Bounded integer deque

```moss
new_int_bounded_deque(capacity: Int) -> IntBoundedDeque
int_deque_len(deque: IntBoundedDeque) -> Int
int_deque_capacity(deque: IntBoundedDeque) -> Int
int_deque_is_empty(deque: IntBoundedDeque) -> Bool
int_deque_is_full(deque: IntBoundedDeque) -> Bool
int_deque_validate(deque: IntBoundedDeque) -> Bool
int_deque_push_front(deque: IntBoundedDeque, value: Int)
int_deque_push_back(deque: IntBoundedDeque, value: Int)
int_deque_peek_front(deque: IntBoundedDeque) -> Int
int_deque_peek_back(deque: IntBoundedDeque) -> Int
int_deque_pop_front(deque: IntBoundedDeque) -> Int
int_deque_pop_back(deque: IntBoundedDeque) -> Int
int_deque_at(deque: IntBoundedDeque, index: Int) -> Int
int_deque_clear(deque: IntBoundedDeque)
int_deque_values(deque: IntBoundedDeque) -> Vector[Int]
```

Deque capacity must be positive. Storage is allocated once; end operations use
conditional wrap arithmetic that avoids evaluating an overflowing `head + size`.

## Validation

From this directory:

```sh
../../margo test
../../margo test --release
../../margo test --interp
```
