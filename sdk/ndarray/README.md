# moss.ndarray

`moss.ndarray` provides owned, contiguous, multidimensional `Float` arrays for
Moss v0.1. It includes shape validation, checked indexing, reshaping, axis
permutation, two-dimensional transpose, trailing-axis broadcasting, copying
slices, axis reductions, and `moss.linalg` Matrix interoperability.

Add the package to a consumer manifest:

```toml
[dependencies]
ndarray = { path = "../sdk/ndarray" }
```

Then use `import ndarray` in Moss source.

## Representation and ownership

`NdArray` has public `shape`, `strides`, and `data` vectors. Storage is C-style
row-major and canonical: the last stride is `1`, and each preceding stride is
the checked product of dimensions to its right. Every public operation validates
an input array before indexing it, so a malformed directly constructed record
raises a typed `NdArrayError` rather than indexing invalid storage.

Constructors copy caller vectors. `shape_copy`, `flatten`, `array_clone`,
`reshape`, arithmetic, slicing, reductions, `permute_axes`, `transpose`, and
Matrix conversions also return owned copies. This package does not claim views,
borrowing, lazy broadcasting, copy-on-write storage, or zero-copy Matrix
conversion.

## Shape contract

- `shape=[]` is a rank-zero scalar with one element and no strides.
- Any zero dimension makes the array empty. `[2, 0, 3]` has canonical strides
  `[0, 3, 1]` and zero elements.
- Negative dimensions raise `NdArrayError.InvalidShape`.
- Any required dimension or suffix product larger than signed Int64 raises
  `NdArrayError.Overflow` before multiplication.
- `get` and `set` require exactly one nonnegative, in-range index per axis.
- `reshape` preserves element count and copies storage.
- `permute_axes` requires each existing axis exactly once. `transpose` is the
  rank-two convenience operation and otherwise raises `InvalidPermutation`.

## Broadcasting and arithmetic

`add`, `subtract`, `multiply`, and `divide` align dimensions from the trailing
axis. Equal dimensions and singleton dimensions are compatible, and a rank-zero
scalar broadcasts to every shape. Broadcasting `0` with `1` produces extent
`0`; broadcasting `0` with an extent greater than `1` raises
`NdArrayError.BroadcastMismatch`. Results are newly allocated contiguous arrays.

`add_scalar`, `subtract_scalar`, `multiply_scalar`, and `divide_scalar` apply a
scalar without constructing a rank-zero array. Division retains Moss's IEEE-754
Float behavior for zero, infinities, and NaN.

## Slices and subarrays

`slice_copy(array, starts, stops, steps)` takes one nonnegative half-open
`[start, stop)` range and one positive step per axis. All three vectors must
match the array rank. Bounds outside the source shape, reversed ranges, and
nonpositive steps raise `NdArrayError.SliceOutOfBounds`. Empty slices and a
rank-zero scalar slice are valid. `subarray_copy(array, origin, shape)` is the
unit-step convenience form. Both functions return contiguous copies.

## Axis reductions

`sum_axis`, `min_axis`, `max_axis`, and `mean_axis` remove one checked axis;
reducing a rank-one array returns a rank-zero scalar. Finite sums use compensated
summation. NaN propagates through every reduction, and minimum/maximum retain the
`moss.math` signed-zero behavior.

An empty sum is `0.0`. Minimum, maximum, and mean raise
`NdArrayError.EmptyReduction` when an output cell would reduce an empty lane. If
the output itself has zero cells, they return the correctly shaped empty array.

## Core API

| Function | Contract |
| --- | --- |
| `validate(array) -> Bool` | Checks shape, canonical strides, overflow safety, and data length without indexing data. |
| `from_flat(shape, data)` | Constructs from copied row-major data. |
| `scalar(value)` | Constructs a rank-zero scalar. |
| `zeros(shape)`, `ones(shape)`, `full(shape, value)` | Construct filled owned arrays. |
| `rank`, `shape_copy`, `size`, `element_count` | Validated metadata access. |
| `get`, `set` | Checked multidimensional access. `set` requires a mutable array. |
| `array_clone`, `flatten` | Independent owned copies. |
| `reshape` | Copy into a new shape with the same element count. |
| `add`, `subtract`, `multiply`, `divide` | Elementwise operations with trailing-axis singleton broadcasting. |
| `add_scalar`, `subtract_scalar`, `multiply_scalar`, `divide_scalar` | Elementwise scalar arithmetic. |
| `slice_copy`, `subarray_copy` | Checked half-open copying slices and unit-step subarrays. |
| `sum_axis`, `min_axis`, `max_axis`, `mean_axis` | Axis-removing reductions with explicit empty-lane behavior. |
| `permute_axes`, `transpose` | Copy while reordering axes. |
| `from_matrix`, `to_matrix` | Copy row-major data to or from `linalg.Matrix`. |

Matrix conversion requires two positive extents because `linalg.Matrix` does
not represent empty dimensions. `to_matrix` rejects non-rank-two arrays with
`MatrixRankMismatch` and empty axes with `MatrixEmptyDimension`.

## Validation

From this directory:

```sh
../../margo test
../../margo --release test
../../margo --interp test
git diff --check
```
