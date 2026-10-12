# moss.bytes

`moss.bytes` uses owned `Vector[Int]` values with each element in `0..255`.
The source module is `bytes`; import it as `import bytes`. Moss v0.1 does not
have private type fields, so this package does not claim to seal the invariant:
a caller can mutate a vector after any SDK operation. Call `bytes.all_bytes`
at an untrusted boundary. Every function returning a copied vector checks its
input or obtains byte values from a live `Range`.

| API | Contract and cost |
| --- | --- |
| `is_byte(value: Int) -> Bool` | True for 0 through 255, inclusive. O(1). |
| `all_bytes(values: Vector[Int]) -> Bool` | Check the whole vector, including empty. O(n). |
| `copy_bytes(values: Vector[Int]) -> Vector[Int]` | Validate each element while making an independent full copy. Invalid input fails `assert`. One pass, O(n). |
| `slice_bytes(values: Vector[Int], start: Int, length: Int) -> Vector[Int]` | Validate and copy the selected portion. Negative start, nonpositive length, or start at/past the end returns empty; a long request clips to the end. Invalid bytes inside the selected portion fail `assert`; bytes outside it are not inspected. O(copied bytes). |
| `get_or(values: Vector[Int], index: Int, fallback: Int) -> Int` | Return fallback for an invalid index; require a byte fallback and require the selected element to be a byte. O(1). |
| `set_if_valid(values: Vector[Int], index: Int, value: Int) -> Bool` | For a valid-input vector, replace one element and return true; invalid index or new byte returns false without mutation. O(1). |
| `append_if_valid(values: Vector[Int], value: Int) -> Bool` | For a valid-input vector, append one byte and return true; invalid byte returns false without mutation. Amortized O(1). |
| `from_range(data) -> Vector[Int]` | Make an owned copy of a borrowed FileIO `Range` before its owner closes. Source generic specialization occurs in the consumer; O(n). |

`set_if_valid` and `append_if_valid` assume `all_bytes(values)` is true on
entry. They validate the new byte but do not rescan unrelated elements, so
repeated appends stay amortized linear overall. `get_or` checks only the
selected element. `copy_bytes` checks every element; `slice_bytes` checks only
the selected elements. An invalid element outside the selected slice does not
make the returned slice invalid.

`from_range` uses a `for` traversal because the current generic specialization
does not classify `Range` as an indexable container. It works for cross-package
native consumers. FileIO and Range execution remain unavailable in Fast Debug;
the other APIs are covered in native and Fast Debug package tests.

From this directory, `../../../margo test --json` passes three native tests and
`../../../margo test --interp` passes the same three in Fast Debug.
`python3 tests/check_negative.py` verifies that invalid existing byte input
fails in both native and Fast Debug execution.
The native cross-package Range example is in `examples/range_copy`; run
`../../../../../margo run` there and expect `5`.
