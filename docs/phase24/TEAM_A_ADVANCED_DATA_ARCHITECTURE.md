# Phase 24 Team A — Advanced Data shared-contract freeze

**Role:** Independent L6 Architect

**Status:** EARLY CONTRACT FREEZE — implementation acceptance is not implied

**Inspection baseline:** `5394e93ceedccf21e577556325e598ff4d8c4d9c` (`phase24-integration`)

**Language/tool contract:** `moss-0.1`, compiler `0.1.0`, `moss-agent-1`

**Date:** 2026-10-12

This document freezes the cross-workstream contracts for `moss.ndarray`,
`moss.datastructures`, `moss.tables`, `moss.binary`, and `moss.cbor`. It is an
independent architecture decision record. It does not implement package source,
approve a future implementation, or authorize compiler/runtime changes.

## 1. Baseline findings that constrain the design

- `moss.linalg.Matrix` is an owned, positive-dimension, row-major
  `Vector[Float]`; its constructors defensively copy and its public operations
  validate forged records before indexing. `moss.sparse` uses the same dense
  layout for conversion and validates dimension products.
- `moss.statistics` defines useful reduction precedents: empty sum is `0.0`,
  empty mean/minimum/maximum are typed errors, and NaN propagates through
  minimum and maximum.
- `moss.collections` demonstrates imported static specialization for untyped
  functions, including compiled-only ABI-v9 consumers. Moss v0.1 does not
  support user-defined generic nominal types, runtime comparator objects, or
  dynamic dispatch. Concrete data-structure storage types are therefore
  required in this phase.
- `moss.bytes` establishes `Vector[Int]` with values in `0..255` as the public
  byte convention. `moss.encoding` strictly converts between UTF-8 bytes and
  Unicode scalar integers, but the baseline has no supported String-to-UTF-8,
  UTF-8-to-String, or scalar-to-String primitive.
- `moss.csv` exposes a string grid and preserves RFC-style quoting, but does not
  carry a typed schema or distinguish quoted empty text from an unquoted empty
  field after parsing.
- `moss.json` confirms that recursive source values are unsuitable and that an
  iterative indexed arena is viable. Team A must not copy its global
  child-vector contiguous-range design: interleaved descendants can invalidate
  a parent's assumed child range.
- ABI-v9 source-free providers retain concrete signatures, inferred ownership
  and observable effects, and generic semantic IR. Fast Debug requires every
  reachable Moss source package; it must reject, not silently mix with, a
  source-free native provider.
- Values have inferred `READ`, `WRITE`, and `CONSUME` effects. Moss does not
  promise arbitrary implicit deep copies, overlapping argument access is legal
  only for `READ + READ`, and no enforceable public borrowing/view abstraction
  exists for these libraries today.
- No public, bit-preserving `Float` to binary64-bits conversion (or inverse) is
  exposed at this baseline. Decimal conversion is not a serialization
  substitute. Native CBOR Float64 therefore remains capability-gated.

## 2. Cross-package rules

1. All packages are ordinary value libraries. They introduce no domains,
   Executors, locks, runtime trait objects, or device annotations.
2. Every exported operation validates public aggregate invariants before
   indexing. A caller can construct a malformed public record; malformed input
   must raise a package-specific typed error, never panic or index accidentally.
3. Dimensions, offsets, counts, capacities, and allocation products use checked
   nonnegative `Int` arithmetic. Check with subtraction/division before addition
   or multiplication; never detect overflow after it occurs.
4. Constructors copy caller-owned vectors into package-owned storage. Returned
   arrays, columns, slices, permutations, selected rows, and decoded byte/text
   payloads are independent owned values. No API is described as zero-copy or a
   view in Phase 24.
5. Read-only operations infer `READ`; setters, queue operations, and cursor
   operations infer `WRITE`. APIs must avoid alias-sensitive calls that overlap
   a writable aggregate with one of its fields.
6. A failing mutator is atomic at its public boundary: it does not advance a
   cursor, partially append output, or leave schema/storage disagreement.
7. Public errors are closed typed enums. Unsupported capability, malformed
   input, invalid argument, overflow, and resource-limit failures remain
   distinguishable.

## 3. `moss.ndarray`

### Representation and shape

The canonical representation is an owned contiguous array:

```moss
type NdArray:
  shape: Vector[Int]
  strides: Vector[Int]
  data: Vector[Float]
```

- Layout is C-style row-major. For a nonempty shape, the final stride is `1`
  and preceding strides are checked products of dimensions to their right.
- Rank is `shape |> count`; element count is the checked product of dimensions.
  The empty product is `1`.
- A rank-zero shape `[]` is a scalar and must contain exactly one value.
- A zero extent is legal. Any shape containing `0` has zero elements; for
  example `[2, 0, 3]` is valid with no data. Negative extents are invalid.
  Canonical strides are still computed right-to-left, so that example has
  strides `[0, 3, 1]`; no index can legally select its zero axis.
- `shape`, `strides`, and `data` lengths/values are validated on every public
  entry. Dimension products larger than `Int` raise `NdArrayError.Overflow`.

Constructors from flat data, zeros, ones, and a scalar fill return owned arrays.
Checked indexing requires exactly `rank` nonnegative indices. Reshape requires
the same element count and returns a copied contiguous array. Flatten, slicing,
axis permutation, and transpose also return copies. A slice uses one
nonnegative half-open `[start, stop)` range per axis with a positive step;
rank mismatch, out-of-range bounds, and nonpositive steps are typed errors.

### Broadcasting, arithmetic, and reductions

- Broadcasting aligns trailing axes. Dimensions are compatible when equal or
  either is `1`; a rank-zero scalar broadcasts to every shape. `0` with `1`
  produces `0`, while `0` with any extent greater than `1` is incompatible.
- Add, subtract, multiply, and divide produce a new row-major array. Division
  follows Moss IEEE-754 Float behavior, including infinities and NaN.
- Axis reductions remove the selected axis. Reducing a rank-one array yields a
  rank-zero scalar. Axis indices are nonnegative and checked.
- Sum of an empty reduced axis uses identity `0.0`. Minimum, maximum, and mean
  over a zero-length reduced axis raise `NdArrayError.EmptyReduction`.
  Minimum/maximum propagate NaN; sum and mean preserve ordinary IEEE behavior.
- Conversion to `linalg.Matrix` is allowed only for valid rank-two arrays whose
  two extents are positive, because the baseline Matrix contract rejects empty
  dimensions. Conversion in both directions copies and preserves row-major
  element order.

No API promises lazy broadcasting, strided views, copy-on-write, device memory,
or fusion. Those may be compiler optimizations only when semantics are unchanged.

## 4. `moss.datastructures`

Because Moss v0.1 cannot declare a user-defined generic heap/deque/map type, the
Phase 24 baseline is deliberately closed and useful rather than falsely generic:

```text
DataValue = Int | Float | Bool | String
IntMinHeap / IntMaxHeap
IntPriorityQueue(priority: Int, sequence: Int, value: DataValue)
BoundedDeque(value: DataValue)
DisjointSet
IntOrderedSet / StringOrderedSet
IntOrderedMap / StringOrderedMap (DataValue values)
```

- Heap `push`/`pop` are `O(log n)` and `peek` is `O(1)`. Empty `peek`/`pop`
  raise a typed error. Max capacity and growth overflow are checked.
- Priority order is integer priority; equal priorities are FIFO by a checked,
  monotonic insertion sequence. Min-priority dequeue is the default and is
  stable. Sequence exhaustion is a typed overflow, not wraparound.
- The bounded deque is a fixed-capacity circular buffer. Both-end push/pop and
  peek are `O(1)`. Capacity must be positive; overflow and empty access are
  distinct errors. Wraparound arithmetic never evaluates an overflowing sum.
- Disjoint-set indices are fixed at construction. `find` uses iterative path
  compression; `union` uses size or rank balancing. Negative/stale indices are
  errors. Amortized complexity is inverse-Ackermann.
- “Ordered” means insertion ordered, not key sorted. Sets/maps use a typed Map
  for average `O(1)` lookup plus an order vector. Insert/replace retains the
  existing position, deletion is `O(n)`, and reinsertion appends at the end.
  Empty structures and duplicate inserts have documented deterministic results.

Untyped helper functions may specialize statically where ABI-v9 tests prove the
contract, but exported storage types must remain concrete. A future true generic
nominal type is a language feature, not a Phase 24 library workaround.

## 5. `moss.tables`

### Closed schema and missing values

The table model is statically closed:

```text
ColumnType = Int | Float | Bool | String
Column = IntColumn | FloatColumn | BoolColumn | StringColumn
Schema = names + types + nullable flags
Table = schema + columns + explicit row_count
```

Each column variant owns a typed values vector and a same-length
`Vector[Bool]` validity bitmap (`true` means present). Missing slots contain a
canonical filler (`0`, `0.0`, `false`, or `""`) that is never a semantic value.
Columns marked non-nullable reject any false validity bit. Names are nonempty
and unique. Every table operation validates schema lengths, variant/schema
agreement, bitmap/value lengths, and the common explicit row count. An explicit
row count lets a zero-column projection retain its number of rows.

Typed access returns an owned values/validity pair and rejects a wrong requested
type. Add/replace copy and validate a column; remove/select preserve schema
order. Row selection accepts checked indices; mask filtering requires one Bool
per row. Missing-value fill/drop operations are type-specific and explicit.

### Relational semantics

- Sort is stable and `O(n log n)`. Missing values are always last. Float NaNs
  are after all non-NaN values but before missing values, independent of sort
  direction. Int, Float, and Bool ordering are required. Native String ordering
  is capability-gated because baseline String has equality but no ordering or
  scalar-code conversion; implementations must return `UnsupportedOrdering`
  rather than invent locale or byte semantics.
- Grouping, distinct, and joins support Int and String keys. Group/distinct
  preserve first-occurrence order and treat all missing keys as one missing
  group. Join missing keys never match, including missing-to-missing.
- Duplicate join keys produce the relational many-to-many Cartesian matches in
  stable left-row then right-row order. A left join emits one row with missing
  right-side values when no match exists. Name collisions require explicit
  suffixes or a typed schema error.
- Aggregates ignore missing input values. `count` counts present values;
  `count_rows` counts rows. An all-missing group returns missing for sum, mean,
  minimum, and maximum. Int sum overflow is typed; Int mean returns Float.
- Expected group/join complexity is average `O(n + m + output)` using typed
  Maps. The output-size term, especially for duplicate joins, must be bounded by
  a configurable row limit and checked before allocation.

CSV import/export requires an explicit schema, header policy, and caller-chosen
missing token. Parsing accepts only exact Moss Int/Float syntax and canonical
`true`/`false` Bool text. Export rejects a present rendered value equal to the
missing token, because the existing CSV grid cannot preserve quoted-vs-unquoted
emptiness; silent lossy round trips are forbidden. JSON adapters are out of the
frozen core until a representation contract proves nested child ownership and
the Unicode limitations are handled explicitly.

## 6. `moss.binary`

`ByteReader` and `ByteWriter` own validated `Vector[Int]` storage and an explicit
cursor/output limit. Reader construction validates every element in `0..255`
and copies it. All reads check `requested <= remaining` before any cursor update.

- Public fixed-width operations name byte order explicitly. Required widths are
  1, 2, 4, and 8 bytes. Unsigned results are limited to `0..INT64_MAX`; an
  encoded 64-bit value above that range raises `BinaryError.Overflow`.
- Signed fixed-width operations use two's-complement interpretation and cover
  all Moss Int values, including `INT64_MIN`, without negating that value.
- Both big-endian and little-endian read/write operations are required. The
  implementation may use checked division/remainder; bitwise syntax is not an
  API requirement.
- Unsigned varints support `0..INT64_MAX`, reject overlong/nonterminating forms,
  and consume at most ten bytes. Length-delimited byte arrays use a varint length
  and enforce both remaining input and configured allocation limits.
- Errors distinguish invalid byte, invalid width/value, unexpected EOF,
  malformed varint, integer overflow, trailing input, and limit exceeded. Error
  records include the cursor/offset and requested/available quantities where
  relevant.

Default limits are 64 MiB total input/output and 16 MiB for one length-delimited
payload. Public constructors may accept stricter limits; nonpositive or larger
than supported limits are rejected. These are safety ceilings, not performance
claims.

## 7. `moss.cbor`

### Supported RFC 8949 subset and representation

CBOR uses an iterative indexed document, not a recursive value:

- Node kinds are null, Bool, Int, Float64, text, byte string, array, and map.
- Text is stored as exact validated UTF-8 bytes, with scalar access through
  `moss.encoding`. This preserves full Unicode semantics without claiming a
  native Moss String conversion that the baseline cannot perform.
- Every container stores its own `first_edge`, `last_edge`, and child/pair count.
  Global edge vectors carry `next_edge` plus child IDs (array) or key/value IDs
  (map). Appending descendants to another container cannot alter an existing
  container's traversal. Validation enforces one parent per owned child, no
  cycles, valid IDs, and exact map pairs.
- Map keys in the Phase 24 practical model are Int or text. Duplicate semantic
  keys are rejected. Map insertion order is preserved.

Encoding uses RFC 8949 major types and the shortest legal integer/length width.
It supports definite-length text, bytes, arrays, and maps; null and Bool; and
signed integers across the Moss Int range. Tags, indefinite-length items,
undefined/other simple values, non-Int/text map keys, bignums, and unsupported
Float widths raise `CborError.UnsupportedValue`; they are never silently
discarded or coerced. This is a standards-compliant subset, not deterministic
CBOR map ordering and not a private format.

Float64 encode/decode is **blocked at this baseline** pending a Team I primitive
that converts between Float and its exact 64 binary bits in both native and Fast
Debug. Until that capability is integrated and independently tested, encountering
a Float node or CBOR float yields `CborError.FloatCapabilityUnavailable`.
Decimal formatting/parsing is expressly forbidden as a wire substitute.

### Decoder ownership, limits, and exactness

The decoder uses an explicit frame stack. A frame records its container node,
remaining child count (map counts are checked before doubling), and tail edge.
Each completed child is linked immediately to its actual parent. `decode_exact`
accepts exactly one item and rejects trailing bytes.

Defaults are maximum depth 64, 1,000,000 total nodes, 1,000,000 container edges,
64 MiB input/output, and 16 MiB per text or byte string. All length conversion,
node/edge growth, and offset arithmetic are checked before mutation. UTF-8 is
validated strictly with `moss.encoding`; malformed/truncated data, duplicate
keys, depth/count/allocation excess, and unsupported values are distinct typed
errors. No decoded value may be accepted and then omitted from its parent.

## 8. Dependency direction

```text
moss.linalg ───────► moss.ndarray
moss.bytes ────────► moss.binary ─────► moss.cbor
moss.encoding ─────────────────────────► moss.cbor
moss.csv / moss.fmt ──────────────────► moss.tables

moss.datastructures   (no Team A dependency)
integration examples ► all Team A packages
```

Arrows point from dependency to consumer. `moss.cbor` does not depend on JSON or
tables; tables do not depend on ndarray; existing packages do not acquire Team A
dependencies. This prevents cycles and keeps binary/CBOR usable independently.

## 9. Bounded FlatBuffers/Arrow feasibility conclusion

- FlatBuffers-compatible owned-buffer construction is mechanically possible
  only as checked bytewise assembly today. `Vector[Int]` is not a packed public
  `u8` buffer, there is no alignment/borrowed-view contract, and exact Float
  bits are unavailable, so efficient general FlatBuffers access is not claimed.
- Arrow IPC is blocked for production use by the same Float/byte-layout issues,
  plus absent zero-copy aligned buffers, validity bitmap primitives, and schema
  generation. `moss.tables` validity semantics are intentionally compatible in
  concept, but no Arrow physical-layout claim is made.
- Team A records these constraints only. A FlatBuffers schema compiler or Arrow
  implementation is outside Phase 24.

## 10. Acceptance gates for a later Architect verdict

The Coordinator must freeze an exact implementation SHA before independent
acceptance. A GO requires evidence for all applicable gates:

1. Package tests in Native Debug and Native Release, plus representative public
   examples in package-resolved Fast Debug. Source-free providers are tested
   separately; Fast Debug must not be claimed for a source-removed graph.
2. Compiled-only ABI-v9 consumers for every package, including exported closed
   enums/records, inferred mutating effects, and any imported generic helper
   actually exposed to consumers.
3. Negative contract suites for forged records, zero/scalar/empty arrays,
   overflow products, invalid axes/slices, empty/stale data-structure access,
   schema/bitmap mismatch, duplicate/null join keys, byte range errors,
   truncated/overlong binary values, CBOR nesting/allocation limits, malformed
   UTF-8, and trailing data.
4. Independent ndarray/table semantic oracles and CBOR RFC 8949 vectors.
   CBOR must also interoperate both directions with a named, version-recorded
   established Python or Rust implementation; encoder-to-local-decoder round
   trips alone do not qualify.
5. A regression that interleaves nested arrays/maps and proves each CBOR
   container retains exactly its own children. This is a release blocker.
6. Matrix conversion parity with `moss.linalg`, CSV typed round trips under the
   explicit missing-token contract, and one public-API-only cross-package app
   spanning ndarray, tables, CBOR, heaps/priority queues, and disjoint sets.
7. Complexity sanity checks with observable outputs. No broad performance or
   zero-copy claims, and no ratio without equivalent release workloads.
8. `TMPDIR="$PWD/tmp" make check`, `make examples`,
   `python3 tests/tooling/check_agent_feedback.py`, and `git diff --check`.
9. The Team efficiency closeout at
   `docs/phase24/agent-patterns/team-a-advanced-data.md`, with actual evidence
   and unknowns preserved per `docs/AGENT_TEAM_EFFICIENCY.md`.

## 11. Highest-risk items and escalation boundary

1. **CBOR Float64:** requires exact Float/binary64 bit conversion. Team A must
   submit a minimal Team I capability request and continue non-Float work.
2. **Native String interoperability:** CBOR text can remain lossless as validated
   UTF-8 bytes/scalars, but native String adapters and String table ordering need
   a reusable scalar/String or comparison primitive. Do not disguise this with
   ASCII-only or decimal workarounds.
3. **Arena ownership:** the prior global contiguous-range pattern is forbidden.
   Edge ownership, reachability, duplicate parentage, and nested sibling order
   need adversarial tests before API stability is claimed.
4. **Closed rather than generic data structures:** concrete closed values are the
   Phase 24 contract. A request for generic nominal declarations is a language
   proposal and must not stall the useful typed baseline.
5. **Output amplification:** duplicate joins, broadcasts, reshapes, and hostile
   CBOR lengths can request enormous allocations. Limits and checked output-size
   arithmetic are correctness requirements, not optional hardening.

Any proposal that changes Moss ownership, recursive-type, String, Float, or ABI
semantics goes to Team I as a narrowly reproduced capability request. Ordinary
API naming, algorithms, and internal storage choices that preserve this freeze
remain Team A decisions.
