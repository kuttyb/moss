# Moss Foundations SDK

Four independent Moss v0.1 packages provide a small, tested foundation for
applications. Add only the packages an application uses. Package names in
`Moss.toml` are `moss.core`, `moss.bytes`, `moss.collections`, and
`moss.algorithms`; source imports use `core`, `bytes`, `collections`, and
`algorithms` respectively.

| Package | Public role | API reference |
| --- | --- | --- |
| `moss.core` | Integer bounds and index checks | [core](core/README.md) |
| `moss.bytes` | Validated owned byte vectors and borrowed FileIO `Range` copying | [bytes](bytes/README.md) |
| `moss.collections` | First-match search, membership, counts, and Int reversal | [collections](collections/README.md) |
| `moss.algorithms` | In-place Int heapsort and sorted search | [algorithms](algorithms/README.md) |

For example, a consumer manifest can contain:

```toml
[dependencies]
"moss.core" = { path = "../sdk/foundations/core" }
"moss.algorithms" = { path = "../sdk/foundations/algorithms" }
```

The path is relative to the consumer manifest. In Moss source, use
`import core` and `import algorithms`, then call `core.valid_index(...)` or
`algorithms.sort_int(...)`.

The [acceptance package](acceptance/README.md) is a runnable example that
imports all four packages. From the repository root, run
`python3 sdk/foundations/acceptance/run_all.py` for the bounded cross-package
gate, or add `--full` for the pinned 44-case randomized corpus. Both run
native debug, native release, Fast Debug, negative contracts, and source-free
ABI-v9 tests. Package-level tests and examples are described in each package
README.

`moss.bytes` represents owned bytes as `Vector[Int]`; callers can mutate that
vector, so public byte operations validate selected input or document a valid
byte-vector precondition. `moss.algorithms` intentionally supports Int order
only; it does not define Float/NaN ordering or a dynamic comparator API.
Moss already provides `map`, `filter`, `reduce`, and other eager collection
pipelines, which remain the normal transformation tools.
