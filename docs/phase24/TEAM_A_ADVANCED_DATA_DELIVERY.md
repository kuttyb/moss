# Phase 24 Team A advanced-data delivery

**Branch:** `phase24-advanced-data`
**Baseline:** `phase24-integration` at `5394e93ceedccf21e577556325e598ff4d8c4d9c`
**Initial reviewed implementation:** `cdcadb26b9b46d332f5f577886c359d44ce839c0` (**NO-GO**)
**Repair implementation:** `3ea1092359cb82b537bbf33595b3d75836df8e79`
**Integration owner:** Team I

## Delivered scope

- `moss.ndarray`: owned row-major Float arrays; checked shape products,
  indexing, reshape, copying, flattening, permutation, transpose, trailing-axis
  broadcasting, scalar arithmetic, copying slices/subarrays, reductions, and
  owned `linalg.Matrix` conversion. Rank-zero arrays contain one scalar; any
  zero extent produces an empty array with canonical strides.
- `moss.datastructures`: validated Int min/max heaps, stable Int priority
  queue, bounded Int deque, union-find with path compression and size
  balancing, Int sorted set/map, and deterministic String insertion set/map.
  No runtime comparators, dynamic dispatch, domain locks, or Executors are
  required.
- `moss.tables`: owned closed typed columns (`Int`, `Float`, `Bool`, `String`)
  with explicit validity vectors, schema/row-count validation, selection,
  typed filtering, stable sorting, distinct values, linked grouping and
  aggregates, bounded stable Int/String joins, projection, and explicit-schema
  CSV import/export.
- `moss.binary`: owned `Vector[Int]` byte reader/writer with byte validation,
  checked cursor arithmetic, big/little-endian signed and unsigned primitives,
  canonical varints, length-delimited values, atomic failures, and explicit
  resource limits.
- `moss.cbor`: RFC 8949 definite-length encoder/iterative decoder for null,
  Bool, Moss-range signed integers, byte strings, validated UTF-8 text bytes,
  arrays, and Int/text-key maps. Its indexed arena uses per-parent linked
  edges, enforces ownership/completeness, and bounds input, depth, items,
  payload, allocation growth, and output.
- Public package READMEs, two higher-level ndarray examples, a bounded
  FlatBuffers/Arrow feasibility report, actionable language-capability
  requests, and a public-API-only cross-package acceptance application.

## Validation on the frozen implementation

| Suite | Unique cases | Native Debug | Native Release | Fast Debug |
| --- | ---: | --- | --- | --- |
| `moss.ndarray` | 21 | 21/21 | 21/21 | 21/21 |
| `moss.datastructures` | 23 | 23/23 | 23/23 | 23/23 |
| `moss.tables` | 24 | 24/24 | 24/24 | 24/24 |
| `moss.binary` | 9 | 9/9 | 9/9 | 9/9 |
| `moss.cbor` | 24 | 24/24 | 24/24 | 24/24 |
| Total package tests | 101 | 101/101 | 101/101 | 101/101 |

Additional acceptance passed:

- two ndarray tensor examples in all three execution modes;
- an independent seeded Python ndarray/table semantic oracle in all three
  execution modes, covering broadcasting, reductions, slices, permutation,
  matrix conversion, sorting, distinct, grouping, aggregation, categorical
  ordering, and duplicate/null-key joins;
- seven cross-package scenarios in all three modes;
- 12 frozen RFC 8949 Appendix A vectors;
- 12 `cbor2 5.7.1` to Moss and 12 Moss to `cbor2 5.7.1`
  interoperability comparisons;
- a compiled-only ABI-v9 consumer for all five Team A modules through 14
  explicit `.mossi`/`.rlib` provider pairs, with no provider Moss source,
  including typed error variant/payload recovery and failed-mutation atomicity;
- `TMPDIR="$PWD/tmp" make check` (`all Moss v0.1 tests passed`, including
  54/54 Executor tests and 61/61 editor tests);
- `make examples`, package-aware formatter checks,
  `python3 tests/tooling/check_agent_feedback.py`, and `git diff --check`.

## Explicit limitations and capability dependencies

- Moss currently exposes no safe bit-preserving Float64-to-bits and
  bits-to-Float64 primitive. CBOR Float construction/Float64 decoding therefore
  raises `FloatCapabilityUnavailable`; Float16/Float32 decoding is explicitly
  unsupported. No lossy substitute is used.
- CBOR exposes validated UTF-8 text as bytes/scalars because no supported
  public String construction/access bridge preserves the required semantics.
  Tags, indefinite-length items, bignums, non-Int/text map keys, and other
  simple values are outside the documented subset and fail with typed errors.
- Tables use owned materialization rather than views. Generic String ordering
  is not claimed; available categorical/String-key operations use documented
  deterministic contracts. JSON adaptation is not claimed because the closed
  flat table model cannot preserve nested values, absent keys, nulls, and
  numeric lexemes without a separate contract.
- CSV import supports all four column kinds. Exact export of present Float
  values and minimum Int is capability-gated with
  `CsvAdapterError.UnsupportedFormatting`; it never emits a lossy value.
- Moss `Int` cannot represent nonnegative `UInt64` values above `INT64_MAX`.
  Binary readers preserve the complete signed 64-bit range but raise typed
  overflow for an unsigned 64-bit wire value outside the Moss carrier range.
- Public heap/priority-queue entry points validate forgeable records, making
  those public calls `O(n)` despite logarithmic internal heap adjustment.
  Ordered Int structures are sorted vectors with documented linear updates;
  no tree-complexity claim is made.
- FlatBuffers-compatible owned construction and a narrow Arrow integer/Bool
  path are feasible only with copies. General Float buffers, packed/aligned
  storage, borrowing, schema generation, and production Arrow IPC remain
  future capabilities. No complete FlatBuffers or Arrow implementation is
  claimed.

No compiler/runtime source was changed, no new testing framework was built,
and no full performance-optimization claim is made. The branch has not been
merged into `phase24-integration` or `main`; Team I retains integration
ownership.

## Independent review and repair

The independent Architect issued **NO-GO** for exact implementation SHA
`cdcadb26b9b46d332f5f577886c359d44ce839c0`; the full attributable report is
preserved in `TEAM_A_ARCHITECT_FINAL_ACCEPTANCE.md`. It found four gaps:

1. the initial data-structure freeze overstated a heterogeneous closed value
   model and uniform insertion ordering beyond the mission-authorized concrete
   typed baseline;
2. tracked acceptance lacked independent ndarray/table semantic oracles;
3. the required Team efficiency closeout was absent; and
4. the tracked compiled-only consumer did not recover typed variants/payloads.

Repair implementation `3ea1092359cb82b537bbf33595b3d75836df8e79`
corrects items 1, 2, and 4. The Team efficiency report corrects item 3. The
repair candidate requires a fresh exact-SHA Architect verdict before delivery;
the earlier NO-GO is not superseded or rewritten until that review occurs.
