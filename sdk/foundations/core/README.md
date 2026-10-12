# moss.core

`moss.core` is the small, dependency-free numeric and bounds foundation for Moss
v0.1. Its source module is `core`; import it as `import core` and call exported
functions with the `core.` qualifier.

| API | Contract |
| --- | --- |
| `min_int(a: Int, b: Int) -> Int` | Smaller operand; equal operands return the same value. |
| `max_int(a: Int, b: Int) -> Int` | Larger operand; equal operands return the same value. |
| `valid_index(index: Int, length: Int) -> Bool` | True exactly when `0 <= index < length`. |
| `valid_slice(start: Int, length: Int, total: Int) -> Bool` | True exactly when the nonnegative slice lies within `0..total`, including an empty slice at the end. Uses `total - start` after checking `start <= total`, avoiding `start + length` overflow. |
| `clamp_int(value: Int, lower: Int, upper: Int) -> Int` | Clamp to inclusive bounds. Requires `lower <= upper`; an inverted interval fails `assert`. |

The bounds helpers do not index or allocate. `valid_index` and `valid_slice`
return `false` for negative dimensions.

From this directory, `../../../margo test --json` passes four native tests and
`../../../margo test --interp` passes the same four in Fast Debug.
