# Advanced data acceptance

This package is the bounded Phase 24 Team A cross-package consumer for the
public APIs of `moss.ndarray`, `moss.datastructures`, `moss.tables`,
`moss.binary`, and `moss.cbor`. It also reaches `linalg.Matrix` and the Tables
CSV adapter through their public interfaces.

The seven scenarios cover:

1. rank-3 array transformation and axis reduction, followed by a rank-2
   `linalg.Matrix` round trip;
2. typed table columns, stable sorting, grouping/aggregation, and a many-to-many
   duplicate-key join;
3. typed CSV export/import including a present value equal to the missing token;
4. stable priority scheduling, bounded-deque wraparound, and union-find grouping;
5. bounded binary length-delimited write/read;
6. CBOR serialization of array/table/join semantic results and semantic decode;
7. typed negative contracts for join, binary, CBOR output/item limits, and
   unsupported Float16/Float64 CBOR behavior.

`fixtures/cbor_rfc8949.json` adds 12 unique fixed vectors copied from RFC 8949
Appendix A. The dependency-free validator freezes their IDs, encodings,
provenance, supported-subset selection, and the optional oracle version. The
ordinary acceptance gate never installs or imports a network dependency.

`validate_ndarray_tables_oracle.py` uses Python reference implementations,
seed `240024`, and no Moss implementation code to derive expected broadcasting,
reduction, slicing, permutation, sorting, distinct, grouping, aggregation,
categorical ordering, and duplicate/null-key join results. It generates a
public-API-only Moss consumer and verifies the independent expectations in
Native Debug, Native Release, and Fast Debug.

## Commands

Run every bounded gate (Native Debug, Native Release, Fast Debug, independent
ndarray/table oracle, fixed vectors, and compiled-only ABI-v9):

```sh
python3 sdk/advanced-data-acceptance/run_all.py
```

The equivalent individual execution commands are:

```sh
cd sdk/advanced-data-acceptance
../../margo run
../../margo --release run
../../margo debug
python3 check_source_free.py
```

Run the optional live oracle with the environment-local `uv`. Its cache and
temporary installation remain disposable inside this worktree:

```sh
UV_CACHE_DIR="$PWD/tmp/uv-cache" TMPDIR="$PWD/tmp" \
  /home/owner/.local/bin/uv run --no-project --with cbor2==5.7.1 \
  python sdk/advanced-data-acceptance/run_all.py --live-cbor2
```

The live oracle performs 24 unique cross-implementation comparisons: 12 values
encoded by pinned `cbor2 5.7.1`, decoded and re-encoded by Moss; and 12 values
encoded by Moss and decoded by `cbor2`. Each side is also checked against the
fixed RFC encoding.

## ABI-v9 method and limits

`check_source_free.py` serially builds and copies 14 explicitly named
`.mossi`/`.rlib` pairs: the five Team A modules plus all required transitive
providers. It checks every copied interface for `native_abi 9`, verifies copy
digests, rejects provider `.moss` files, then checks, builds, and executes a
consumer whose manifest has no source dependencies. The consumer exercises
exported records and mutators plus typed error-variant payload recovery from all
five Team A modules, including failed-mutation atomicity. There are no globs
and no parallel Margo invocations.

The acceptance workload is intentionally small and deterministic. It makes no
throughput, allocation, asymptotic superiority, zero-copy, accelerator, or
production-scale claim. Current ndarray/table operations materialize owned
results; table joins are bounded by an explicit output-row limit; and the
data-structure checks validate behavior rather than benchmark complexity.

CBOR supports the documented definite-length subset. Float construction and
Float64 access/encoding remain capability-gated; Float16/Float32 decoding is
explicitly unsupported. CBOR text is exposed as validated UTF-8 byte/scalar
vectors because the public API has no direct `String` constructor/accessor.
Tags, indefinite lengths, non-Int/text map keys, other simple values, and
bignums are outside this acceptance contract.
