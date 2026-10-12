# moss.algorithms

Integer sequence algorithms built on `Vector[Int]`. The package exports the `algorithms` module and has no SDK dependencies.

| Function | Contract | Cost |
| --- | --- | --- |
| `sort_int(items: Vector[Int])` | Sorts a writable vector ascending in place; unstable order for equal keys. | Worst-case O(n log n) time, O(1) extra storage |
| `lower_bound_int(items: Vector[Int], target: Int) -> Int` | On ascending input, first index with value at least `target`; returns length if none. | O(log n) time, O(1) storage |
| `binary_search_int(items: Vector[Int], target: Int) -> Int` | On ascending input, first equal index; returns `-1` if absent. | O(log n) time, O(1) storage |

`sort_int` uses bottom-up heapsort and requires a writable lvalue (`var` or an equivalent writable place). The compiler reports `IMMUTABLE_LOCAL_MUTATION` for an explicit `let` input; see `examples/immutable_rejected`. The sorted-search precondition is the caller's responsibility. These functions do not invent comparator or dynamic callable semantics. `lower_bound_int` computes the midpoint as `low + (high - low) / 2` so it does not overflow from adding two large indices.

```moss
module app
import algorithms

fn main():
  var values = [5, 2, 2, 1]
  algorithms.sort_int(values)
  echo algorithms.lower_bound_int(values, 2)    # 1
  echo algorithms.binary_search_int(values, 2)  # 1
```

The package manifest is at `Moss.toml`. Add `"moss.algorithms" = { path = ".../sdk/foundations/algorithms" }` under `[dependencies]` and `import algorithms` in the consumer. `examples/basic` is a runnable path-dependency example.

Run `margo test` from this package for native tests, `margo test --interp` for Fast Debug tests, `margo run` and `margo debug` from `examples/basic` for cross-package parity, `python3 tests/check_source_free.py` for compiled-only provider execution, and `margo bench --release` for release-profile measurements. For cross-package benchmarks, `--release` is required to build provider rlibs with optimization. Benchmarks include input construction in the measured body; their numbers are end-to-end costs, not isolated sort/search latency.
