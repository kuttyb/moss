# moss.collections

Small collection operations that are not already built into Moss. The package exports the `collections` module and has no SDK dependencies.

| Function | Contract | Cost |
| --- | --- | --- |
| `index_of(items, target) -> Int` | First equal index, or `-1`; specializes for an indexable vector whose elements support `==` with `target`. | O(n) time, O(1) extra storage |
| `contains(items, target) -> Bool` | Whether any element equals `target`. | O(n) time, O(1) extra storage |
| `count_equal(items, target) -> Int` | Number of elements equal to `target`. | O(n) time, O(1) extra storage |
| `reversed_copy_int(items: Vector[Int]) -> Vector[Int]` | New reversed vector; source remains unchanged. | O(n) time, O(n) storage |

`index_of`, `contains`, and `count_equal` use static specialization. The public tests exercise both `Vector[Int]` and `Vector[String]`; element types without equality are rejected when called. All functions READ their input. A search never indexes beyond the measured vector length. `reversed_copy_int` is deliberately concrete because constructing an empty generic result vector is not part of Moss v0.1's source type system.

The built-in `map`, `filter`, `reduce`, `sum`, `count`, `any`, and `all` remain the preferred transformations. `Map.get`, `Map.keys`, `Map.values`, `Map.delete`, and `Queue` operations are also already built in.

```moss
module app
import collections

fn main():
  values = [4, 2, 4]
  echo collections.index_of(values, 4)      # 0
  echo collections.count_equal(values, 4)   # 2
```

The package manifest is at `Moss.toml`. Add `"moss.collections" = { path = ".../sdk/foundations/collections" }` under `[dependencies]` and `import collections` in the consumer. `examples/basic` is a runnable path-dependency example.

Run `margo test` from this package for native tests, `margo test --interp` for Fast Debug tests, `margo run` and `margo debug` from `examples/basic` for cross-package parity, `python3 tests/check_source_free.py` for compiled-only provider execution, and `margo bench` for release-profile measurements. Benchmarks include vector construction in the measured body; their numbers are end-to-end costs, not isolated call latency.
