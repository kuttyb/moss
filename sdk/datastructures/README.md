# moss.datastructures

`moss.datastructures` provides concrete priority and search-oriented structures
for Moss v0.1: integer min/max heaps, a stable integer priority queue, a
fixed-size integer ring deque, union-find, ascending integer sets and maps, and
first-insertion-ordered String sets and maps. The package has no dependencies
and uses no domains, Executors, dynamic comparators, or recursion.

Add it to a consumer manifest with package name `moss.datastructures`, then use
`import datastructures`.

## Ownership and validation

Constructors own their storage. `min_heap_from` and `max_heap_from` copy the
input vector before applying bottom-up heapify, so later heap mutation does not
change the caller's vector. Collection snapshots and returned String keys are
owned copies. Updating an existing insertion-ordered map key retains its
position; removing and reinserting a String appends it at the end.

Moss v0.1 exported records are directly constructible. Every public operation
therefore validates its record before indexing and raises
`DataStructureError.InvalidState` for malformed heap order, mismatched priority
queue vectors, invalid or duplicate sequence IDs, invalid queue order, or bad
deque metadata. The explicit `*_validate` functions return `false` instead.

This safety rule has an important complexity consequence: full public-record
validation is O(n). The heapify constructor is O(n), and the underlying
sift-up/sift-down mutation is O(log n), but a public heap or priority-queue
operation is O(n) including mandatory validation. Deque validation is O(1), so
both-end deque operations remain O(1).

Union-find uses iterative path compression and union by size. Its algorithmic
find/union cost is amortized inverse-Ackermann, but validation of a directly
constructible record checks every parent chain and is O(n^2) in the worst case;
therefore every public operation is O(n^2) in the current safety contract.

Ascending integer set/map lookup uses binary search. Validation is O(n), so a
public lookup is O(n), while insertion/removal is O(n) because vectors must be
shifted. String insertion collections use linear lookup and O(n^2) uniqueness
validation; public lookup/update is therefore O(n^2). String removal rebuilds
owned storage in O(n) after validation. A future opaque-record facility could
retain forged-record rejection while exposing the lower underlying algorithmic
costs; this package does not hide the current validation overhead.

## Errors

`DataStructureError` is a closed typed enum. This workstream uses:

- `Empty` for peek/pop on an empty structure.
- `Full(capacity)` for insertion into a full deque.
- `InvalidCapacity(capacity)` for deque capacity less than or equal to zero.
- `InvalidIndex(index, length)` for deque random access outside `0..length`.
- `SequenceOverflow` when the stable priority sequence is exhausted.
- `CapacityOverflow` before a vector count could overflow Moss `Int`.
- `InvalidState` for malformed directly constructed records.
- `InvalidSize(size)` for a negative union-find size.
- `NotFound` for strict map lookup of an absent key.

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

### Union-find

```moss
new_union_find(size: Int) -> UnionFind
union_find_len(sets: UnionFind) -> Int
union_find_set_count(sets: UnionFind) -> Int
union_find_validate(sets: UnionFind) -> Bool
union_find_find(sets: UnionFind, item: Int) -> Int
union_find_same(sets: UnionFind, left: Int, right: Int) -> Bool
union_find_union(sets: UnionFind, left: Int, right: Int) -> Bool
union_find_component_size(sets: UnionFind, item: Int) -> Int
```

Size zero is valid. `union_find_union` returns `true` only when two components
were merged. Ties attach the right root to the left root. Find, same-component,
union, and component-size operations perform iterative path compression.

### Ascending integer set and map

```moss
new_int_sorted_set() -> IntSortedSet
int_sorted_set_len(collection: IntSortedSet) -> Int
int_sorted_set_validate(collection: IntSortedSet) -> Bool
int_sorted_set_contains(collection: IntSortedSet, value: Int) -> Bool
int_sorted_set_add(collection: IntSortedSet, value: Int) -> Bool
int_sorted_set_remove(collection: IntSortedSet, value: Int) -> Bool
int_sorted_set_at(collection: IntSortedSet, index: Int) -> Int
int_sorted_set_values(collection: IntSortedSet) -> Vector[Int]

new_int_sorted_map() -> IntSortedMap
int_sorted_map_len(mapping: IntSortedMap) -> Int
int_sorted_map_validate(mapping: IntSortedMap) -> Bool
int_sorted_map_contains(mapping: IntSortedMap, key: Int) -> Bool
int_sorted_map_get(mapping: IntSortedMap, key: Int) -> Int
int_sorted_map_get_or(mapping: IntSortedMap, key: Int, fallback: Int) -> Int
int_sorted_map_put(mapping: IntSortedMap, key: Int, value: Int) -> Bool
int_sorted_map_remove(mapping: IntSortedMap, key: Int) -> Bool
int_sorted_map_key_at(mapping: IntSortedMap, index: Int) -> Int
int_sorted_map_value_at(mapping: IntSortedMap, index: Int) -> Int
```

Set add returns `false` for a duplicate. Map put returns `true` for insertion
and `false` for replacement. Removal returns `false` when absent. Indexed access
always follows ascending key/value order.

### First-insertion-ordered String set and map

```moss
new_string_insertion_set() -> StringInsertionSet
string_insertion_set_len(collection: StringInsertionSet) -> Int
string_insertion_set_validate(collection: StringInsertionSet) -> Bool
string_insertion_set_contains(collection: StringInsertionSet, value: String) -> Bool
string_insertion_set_add(collection: StringInsertionSet, value: String) -> Bool
string_insertion_set_remove(collection: StringInsertionSet, value: String) -> Bool
string_insertion_set_at(collection: StringInsertionSet, index: Int) -> String
string_insertion_set_values(collection: StringInsertionSet) -> Vector[String]

new_string_insertion_map() -> StringInsertionMap
string_insertion_map_len(mapping: StringInsertionMap) -> Int
string_insertion_map_validate(mapping: StringInsertionMap) -> Bool
string_insertion_map_contains(mapping: StringInsertionMap, key: String) -> Bool
string_insertion_map_get(mapping: StringInsertionMap, key: String) -> Int
string_insertion_map_get_or(mapping: StringInsertionMap, key: String, fallback: Int) -> Int
string_insertion_map_put(mapping: StringInsertionMap, key: String, value: Int) -> Bool
string_insertion_map_remove(mapping: StringInsertionMap, key: String) -> Bool
string_insertion_map_key_at(mapping: StringInsertionMap, index: Int) -> String
string_insertion_map_value_at(mapping: StringInsertionMap, index: Int) -> Int
```

Ordering is first insertion, not lexical order. Empty and Unicode Strings are
ordinary keys. Duplicate set insertion is a no-op; map replacement retains the
original position; removal followed by insertion appends at the end.

## Validation

From this directory:

```sh
../../margo test
../../margo test --release
../../margo test --interp
```
